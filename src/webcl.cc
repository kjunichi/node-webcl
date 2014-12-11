// Copyright (c) 2011-2012, Motorola Mobility, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the Motorola Mobility, Inc. nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "webcl.h"
#include "platform.h"
#include "context.h"
#include "device.h"
#include "event.h"
#include "commandqueue.h"

#include <set>
#include <vector>
#include <algorithm>
#include <cstring>

using namespace v8;
using namespace node;
using namespace std;

namespace webcl {

static set<WebCLObject*> clobjs;
static bool atExit=false;

void registerCLObj(WebCLObject* obj) {
  if(obj) {
    // printf("Adding CLObject %p\n", obj);
    clobjs.insert(obj);
  }
}

void unregisterCLObj(WebCLObject* obj) {
  if(atExit || !obj) return;

  // printf("Removing CLObject %p\n", obj);
  clobjs.erase(obj);
}

void AtExit(void* /*arg*/) {
  atExit=true;
  #ifdef LOGGING
  cout<<"WebCL AtExit() called"<<endl;
  cout<<"  # objects allocated: "<<clobjs.size()<<endl;
  #endif

  // make sure all queues are flushed
  set<WebCLObject*>::iterator it = clobjs.begin();
  while(it != clobjs.end()) {
    WebCLObject *clo = *it;
    ++it;
    if(clo->isCommandQueue()) {
#ifdef LOGGING
      cout<<"  Flushing commandqueue"<<endl;
#endif
      CommandQueue *queue=static_cast<CommandQueue*>(clo);
      // PATCH: Destroyed by release from JS
      cl_command_queue q=queue->getCommandQueue();
      if ( q ) {
        clFlush(q);
      }
    }
  }

  // must kill events first
  it = clobjs.begin();
  while(it != clobjs.end()) {
    WebCLObject *clo = *it;
    ++it;
    if(clo->isEvent()) {
#ifdef LOGGING
      cout<<"  Destroying event"<<endl;
#endif
      clobjs.erase(clo);
      clo->Destructor();
    }
  }

  // must kill kernels first
  it = clobjs.begin();
  while(it != clobjs.end()) {
    WebCLObject *clo = *it;
    ++it;
    if(clo->isKernel()) {
#ifdef LOGGING
      cout<<"  Destroying kernel"<<endl;
#endif
      clobjs.erase(clo);
      clo->Destructor();
    }
  }

  #ifdef LOGGING
  cout<<"  # objects allocated: "<<clobjs.size()<<endl;
  #endif
  it = clobjs.begin();
  while(it != clobjs.end()) {
    WebCLObject *clo = *it;
#ifdef LOGGING
      cout<<"  [AtExit] Destroying ";
      if(clo->isCommandQueue())
        cout<<"CommandQueue";
      else if(clo->isKernel())
        cout<<"Kernel";
      else if(clo->isEvent())
        cout<<"Event";
      else if(clo->isProgram())
        cout<<"Program";
      else if(clo->isMemoryObject())
        cout<<"MemoryObject";
      else if(clo->isSampler())
        cout<<"Sampler";
      else if(clo->isContext())
        cout<<"Context";
      else
        cout<<"UNKNOWN";
      cout<<endl;
#endif
    ++it;
    clo->Destructor();
  }

  clobjs.clear();
}

NAN_METHOD(getPlatforms) {
  NanScope();

  cl_uint num_entries = 0;
  cl_int ret = ::clGetPlatformIDs(0, NULL, &num_entries);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  cl_platform_id* platforms=new cl_platform_id[num_entries];
  ret = ::clGetPlatformIDs(num_entries, platforms, NULL);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }


  Local<Array> platformArray = Array::New(v8::Isolate::GetCurrent(),num_entries);
  for (uint32_t i=0; i<num_entries; i++) {
    platformArray->Set(i, NanObjectWrapHandle(Platform::New(platforms[i])));
  }

  delete[] platforms;

  NanReturnValue(platformArray);
}

NAN_METHOD(releaseAll) {
  NanScope();
  
  AtExit(NULL);
  atExit=true;

  NanReturnUndefined();
}

class ContextWorker : public NanAsyncWorker {
 public:
  ContextWorker(Baton *baton)
    : NanAsyncWorker(baton->callback), baton_(baton) {
    }

  ~ContextWorker() {
    if(baton_) {
      NanScope();
      if (!baton_->data.IsEmpty()) NanDisposePersistent(baton_->data);
      delete baton_;
    }
  }

  // Executed inside the worker-thread.
  // It is not safe to access V8, or V8 data structures
  // here, so everything we need for input and output
  // should go on `this`.
  void Execute () {
    // SetErrorMessage("Error");
    // printf("[async event] execute\n");
  }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    NanScope();

    Local<Value> argv[2];
    if(baton_->error_msg)
      argv[0]=JS_STR(baton_->error_msg);
    else
      argv[0]=JS_INT(CL_SUCCESS);
    argv[1] = NanNew(baton_->data);

    // printf("[async event] callback JS\n");
    callback->Call(2, argv);
  }

  private:
    Baton *baton_;
};

void createContext_callback (const char *errinfo, const void *private_info, size_t cb, void *user_data)
{
  //cout<<"[createContext_callback] thread "<<pthread_self()<<" errinfo ="<<(errinfo ? errinfo : "none")<<endl;
  Baton *baton = static_cast<Baton*>(user_data);
  baton->error=0;

  if(errinfo) {
    baton->error_msg=strdup(errinfo);
    baton->error=1;
  }

  if(private_info && cb) {
    baton->priv_info=new uint8_t[cb];
    memcpy(baton->priv_info, private_info, cb);
  }

  NanAsyncQueueWorker(new ContextWorker(baton));
}

NAN_METHOD(createContext) {
  NanScope();
  cl_int ret=CL_SUCCESS;
  cl_context cw=NULL;

  // callback handling
  Baton *baton=NULL;
  if(args.Length()==3 && !args[2]->IsUndefined() && args[2]->IsFunction()) {
    baton=new Baton();
    baton->callback=new NanCallback(args[2].As<Function>());
    if(!args[1]->IsUndefined()) {
      Local<Value> data=args[1];
      NanAssignPersistent(baton->data, data);
    }
  }

  // property handling
  if(!args[0]->IsUndefined() && args[0]->IsObject()) {
    Platform *platform = NULL;
    vector<cl_device_id> devices;
    vector<cl_context_properties> properties;
    Local<Array> props = Local<Array>::Cast(args[0]);

    if(props->Has(JS_STR("platform"))) {
      Local<Object> obj = props->Get(JS_STR("platform"))->ToObject();
      if(!obj->IsNull()) {
        platform=ObjectWrap::Unwrap<Platform>(obj);
        properties.push_back(CL_CONTEXT_PLATFORM);
        properties.push_back((cl_context_properties) platform->getPlatformId());
        //cout<<"adding platform "<<hex<<platform->getPlatformId()<<dec<<endl;
      }
    }
    else {
      // we must use the default platform
      cl_uint numPlatforms=0; //the NO. of platforms
      cl_int  status = ::clGetPlatformIDs(0, NULL, &numPlatforms);
      if (status != CL_SUCCESS)
      {
        NanThrowError("Can NOT get an OpenCL platform!");
      }

      /*For clarity, choose the first available platform. */
      if (numPlatforms > 0)
      {
        cl_platform_id* platforms = new cl_platform_id[numPlatforms];
        status = clGetPlatformIDs(numPlatforms, platforms, NULL);
        properties.push_back(CL_CONTEXT_PLATFORM);
        properties.push_back((cl_context_properties) platforms[0]);

        delete[] platforms;
      }

    }

    if(props->Has(JS_STR("shareGroup"))) {
      // TODO get WebGL object and retrieve the context from it
      //Local<Object> obj = props->Get(JS_STR("shareGroup"))->ToObject();
      //if(!obj->IsNull()) {
        //platform=ObjectWrap::Unwrap<Platform>(obj);
#if defined (__APPLE__)
        CGLContextObj kCGLContext = CGLGetCurrentContext();
        CGLShareGroupObj kCGLShareGroup = CGLGetShareGroup(kCGLContext);
        properties.push_back(CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE);
        properties.push_back((cl_context_properties) kCGLShareGroup);
#else
  #ifdef _WIN32
        properties.push_back(CL_GL_CONTEXT_KHR);
        properties.push_back((cl_context_properties) wglGetCurrentContext());
        properties.push_back(CL_WGL_HDC_KHR);
        properties.push_back((cl_context_properties) wglGetCurrentDC());
  #else // Unix
        properties.push_back(CL_GL_CONTEXT_KHR);
        properties.push_back((cl_context_properties) glXGetCurrentContext());
        properties.push_back(CL_GLX_DISPLAY_KHR);
        properties.push_back((cl_context_properties) glXGetCurrentDisplay());
  #endif
#endif
      //}
    }

    // terminate properties array if any
    if(properties.size()) properties.push_back(0);

    if(props->Has(JS_STR("deviceType"))) {
      cl_uint device_type=props->Get(JS_STR("deviceType"))->Uint32Value();
      cw = ::clCreateContextFromType(properties.size() ? &properties.front() : NULL,
                                     device_type,
                                     /*baton ? createContext_callback :*/ NULL,
                                     baton , &ret);
    }
    else if(props->Has(JS_STR("devices"))) {
      Local<Object> obj = props->Get(JS_STR("devices"))->ToObject();
      if(!obj->IsNull()) {
        if(obj->IsArray()) {
          Local<Array> deviceArray = Local<Array>::Cast(obj);
          for (uint32_t i=0; i<deviceArray->Length(); i++) {
            Local<Object> obj = deviceArray->Get(i)->ToObject();
            Device *d = ObjectWrap::Unwrap<Device>(obj);
            #ifdef LOGGING
            cout<<"adding device "<<hex<<d->getDevice()
            <<dec<<endl;
            #endif
            devices.push_back(d->getDevice());
          }
        }
        else {
          Device *d = ObjectWrap::Unwrap<Device>(obj);
          devices.push_back(d->getDevice());
        }
      }

      //cout<<"[createContext] creating context with devices"<<endl<<flush;
      cw = ::clCreateContext(properties.size() ? &properties.front() : NULL,
                             (int) devices.size(), &devices.front(),
                             /*baton ? createContext_callback :*/ NULL,
                             baton , &ret);
    }
    else {
      NanThrowError("Invalid parameters");
    }
  }

  // automatic context creation
  else if(args[0]->IsUndefined()) {
#if defined (__APPLE__) || defined(MACOSX)
    CGLContextObj kCGLContext = CGLGetCurrentContext();
    #ifdef LOGGING
    cout<<"using CGL context: "<<hex<<kCGLContext<<dec<<endl;
    #endif
    CGLShareGroupObj kCGLShareGroup = CGLGetShareGroup(kCGLContext);
    cl_context_properties props[] =
    {
        CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE, (cl_context_properties)kCGLShareGroup,
        0
    };
    #ifdef LOGGING
    cout<<"[createContext] creating context"<<endl<<flush;
    #endif
    cw = clCreateContext(props, 0, 0,
        /*baton ? createContext_callback :*/ clLogMessagesToStdoutAPPLE,
        baton /*0*/, &ret);

    if (!cw)
    {
        NanThrowError("Error: Failed to create a compute context!");
        //NanReturnValue(Undefined());
NanReturnUndefined();
    }
    #ifdef LOGGING
    cout<<"Apple OpenCL SharedGroup context created"<<endl;
    #endif
#else
    #ifndef _WIN32
    cl_context_properties props[] =
    {
        CL_GL_CONTEXT_KHR, (cl_context_properties)glXGetCurrentContext(),
        CL_GLX_DISPLAY_KHR, (cl_context_properties)glXGetCurrentDisplay(),
        0
    };
    cw = clCreateContext(props, 0, 0,
        baton ? createContext_callback : NULL,
        baton , &ret);

    if (!cw)
    {
        NanReturnValue(NanThrowError("Error: Failed to create a compute context!"));
    }
    #else
    // TODO add automatic context creation for Unix and Win32
    NanReturnValue(NanThrowError("Unsupported createContext() without parameters"));
    #endif
#endif
  }

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_PLATFORM);
    REQ_ERROR_THROW(CL_INVALID_PROPERTY);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_DEVICE_TYPE);
    REQ_ERROR_THROW(CL_DEVICE_NOT_AVAILABLE);
    REQ_ERROR_THROW(CL_DEVICE_NOT_FOUND);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  NanReturnValue(NanObjectWrapHandle(Context::New(cw)));
}

class WaitForEventsWorker : public NanAsyncWorker {
 public:
  WaitForEventsWorker(Baton *baton)
    : NanAsyncWorker(baton->callback), baton_(baton) {
    }

  ~WaitForEventsWorker() {
    if(baton_) {
      NanScope();
      if (!baton_->parent.IsEmpty()) NanDisposePersistent(baton_->parent);
      if (!baton_->data.IsEmpty()) NanDisposePersistent(baton_->data);
      // if (baton_->callback) delete baton_->callback;
      delete baton_;
    }
  }

  // Executed inside the worker-thread.
  // It is not safe to access V8, or V8 data structures
  // here, so everything we need for input and output
  // should go on `this`.
  void Execute () {
    // SetErrorMessage("Error");
    // printf("[async event] execute\n");
    Local<Array> eventsArray = Local<Array>::Cast(NanNew(baton_->data));
    std::vector<cl_event> events;
    for (uint32_t i=0; i<eventsArray->Length(); i++) {
     Event *we=ObjectWrap::Unwrap<Event>(eventsArray->Get(i)->ToObject());
      cl_event e = we->getEvent();
      events.push_back(e);
    }
    cl_int ret = baton_->error = ::clWaitForEvents( (int) events.size(), &events.front());
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(CL_INVALID_VALUE);
      REQ_ERROR_THROW(CL_INVALID_CONTEXT);
      REQ_ERROR_THROW(CL_INVALID_EVENT);
      REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
      REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
      REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
      return;// NanThrowError("UNKNOWN ERROR");
    }
  }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    NanScope();

    // must return passed data
    Local<Value> argv[] = {
      JS_INT(baton_->error)
    };

    // printf("[async event] callback JS\n");
    callback->Call(1, argv);
  }

  private:
    Baton *baton_;
};

NAN_METHOD(waitForEvents) {
  NanScope();

  if (!args[0]->IsArray())
    NanThrowError("CL_INVALID_VALUE");

  if(args[1]->IsFunction()) {
      Baton *baton=new Baton();
      NanAssignPersistent(baton->data, args[0]);
      baton->callback=new NanCallback(args[1].As<Function>());
      NanAsyncQueueWorker(new WaitForEventsWorker(baton));
      NanReturnUndefined();
  }

  Local<Array> eventsArray = Local<Array>::Cast(args[0]);
  std::vector<cl_event> events;
  for (uint32_t i=0; i<eventsArray->Length(); i++) {
    Event *we=ObjectWrap::Unwrap<Event>(eventsArray->Get(i)->ToObject());
    cl_event e = we->getEvent();
    events.push_back(e);
  }

  // printf("Waiting for %d events\n",events.size());
  // for(int i=0;i<events.size();i++)
    // printf("   %p\n",events[i]);
  cl_int ret=::clWaitForEvents( (int) events.size(), &events.front());
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_EVENT);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  NanReturnUndefined();
}



}
