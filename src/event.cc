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

#include "event.h"
#include "context.h"
#include "commandqueue.h"

using namespace node;
using namespace v8;

namespace webcl {

Persistent<FunctionTemplate> Event::constructor_template;

void Event::Init(Handle<Object> target)
{
  NanScope();

  // constructor
  Local<FunctionTemplate> ctor = NanNew<FunctionTemplate>(Event::New);
  NanAssignPersistent(constructor_template, ctor);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(JS_STR("WebCLEvent"));

  // prototype
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getInfo", getInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getProfilingInfo", getProfilingInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_setCallback", setCallback);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_release", release);

  Local<ObjectTemplate> proto = ctor->PrototypeTemplate();
  proto->SetAccessor(JS_STR("status"), GetStatus, NULL);
  
  target->Set(JS_STR("WebCLEvent"), ctor->GetFunction());
}

Event::Event(Handle<Object> wrapper) : /*callback(NULL),*/ event(0), status(0)
{
  _type=CLObjType::Event;
}

void Event::Destructor()
{
#ifdef LOGGING
    printf("  Destroying CL event %p\n",this);
#endif
  if(event) {
    ::clReleaseEvent(event);
  }
  event=0;
}

NAN_METHOD(Event::release)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());
  #ifdef LOGGING
  printf("  In Event::release %p\n",e);
  #endif
  
  DESTROY_WEBCL_OBJECT(e);
  
  NanReturnUndefined();
}

NAN_METHOD(Event::getInfo)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());
  cl_event_info param_name = args[0]->Uint32Value();
  cl_int ret=CL_SUCCESS;

  switch (param_name) {
  case CL_EVENT_CONTEXT:{
    cl_context param_value=NULL;
    ret=::clGetEventInfo(e->getEvent(), param_name, sizeof(cl_context), &param_value, NULL);
    if(ret!=CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_EVENT);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("Unknown error");
    }
    NanReturnValue(NanObjectWrapHandle(Context::New(param_value)));
  }
  case CL_EVENT_COMMAND_QUEUE:{
    cl_command_queue param_value=NULL;
    ret=::clGetEventInfo(e->getEvent(), param_name, sizeof(cl_command_queue), &param_value, NULL);
    if(ret!=CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_EVENT);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("Unknown error");
    }
    if(param_value) {
      WebCLObject *obj=findCLObj((void*)param_value);
      if(obj) {
        //::clRetainCommandQueue(param_value);
        NanReturnValue(NanObjectWrapHandle(obj));
      }
    }
    NanReturnUndefined();
  }
  case CL_EVENT_REFERENCE_COUNT:
  case CL_EVENT_COMMAND_TYPE:
  case CL_EVENT_COMMAND_EXECUTION_STATUS: {
    cl_uint param_value=0;
    ret=::clGetEventInfo(e->getEvent(), param_name, sizeof(cl_uint), &param_value, NULL);
    if(ret!=CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_EVENT);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("Unknown error");
    }
    NanReturnValue(NanNew(param_value));
  }
  default:
    return NanThrowError("UNKNOWN param_name");
  }

}

NAN_METHOD(Event::getProfilingInfo)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());
  cl_event_info param_name = args[0]->Uint32Value();
  cl_int ret=CL_SUCCESS;

  switch (param_name) {
  case CL_PROFILING_COMMAND_QUEUED:
  case CL_PROFILING_COMMAND_SUBMIT:
  case CL_PROFILING_COMMAND_START:
  case CL_PROFILING_COMMAND_END: {
    cl_ulong param_value=0;
    ret=::clGetEventProfilingInfo(e->getEvent(), param_name, sizeof(cl_ulong), &param_value, NULL);
    if(ret!=CL_SUCCESS) {
      REQ_ERROR_THROW(PROFILING_INFO_NOT_AVAILABLE);
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_EVENT);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("Unknown error");
    }
    NanReturnValue(JS_INT((int32_t)param_value));
  }
  default:
    return NanThrowError("UNKNOWN param_name");
  }
}

void Event::setEvent(cl_event e) {
  Destructor();
  event=e;
}

class EventWorker : public NanAsyncWorker {
 public:
  EventWorker(Baton *baton)
    : NanAsyncWorker(baton->callback), baton_(baton) {
    }

  ~EventWorker() {
    if(baton_) {
      NanScope();
      if (!baton_->data.IsEmpty()) NanDisposePersistent(baton_->data);
      if (!baton_->parent.IsEmpty()) NanDisposePersistent(baton_->parent);
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
  }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    NanScope();
    // printf("[async event] in HandleOKCallback\n");

    // sets event status
    Local<Object> p = NanNew(baton_->parent);
    Event *e = ObjectWrap::Unwrap<Event>(p);
    e->setStatus(baton_->error);

    // // must return passed data
    if(baton_->data.IsEmpty()) {
      Local<Value> argv[] = { NanNew(baton_->parent) };
      callback->Call(1, argv);
    }
    else {
      Local<Value> argv[] = {
        NanNew(baton_->parent),  // event
        NanNew(baton_->data)     // user's message
      };

      // printf("[async event] callback JS\n");
      callback->Call(2, argv);
    }
  }

  private:
    Baton *baton_;
};

void CL_CALLBACK Event::callback (cl_event event, cl_int event_command_exec_status, void *user_data)
{
  // printf("[Event::callback] event=%p, exec status=%d\n",event,event_command_exec_status);
  Baton *baton = static_cast<Baton*>(user_data);
  baton->error = event_command_exec_status;

  // printf("EventWorker launched\n");
  NanAsyncQueueWorker(new EventWorker(baton));
}

NAN_METHOD(Event::setCallback)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());
  cl_int command_exec_callback_type = args[0]->Int32Value();

  Baton *baton=new Baton();
  if(!args[2]->IsNull() && !args[2]->IsUndefined()) 
    NanAssignPersistent(baton->data, args[2]);
  NanAssignPersistent(baton->parent, NanObjectWrapHandle(e));
  baton->callback=new NanCallback(args[1].As<Function>());

  // printf("SetEventCallback event=%p for callback %p\n",e->getEvent(), baton->callback);
  cl_int ret=::clSetEventCallback(e->getEvent(), command_exec_callback_type, callback, baton);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(INVALID_EVENT);
    REQ_ERROR_THROW(INVALID_VALUE);
    REQ_ERROR_THROW(OUT_OF_RESOURCES);
    REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  NanReturnUndefined();
}

NAN_GETTER(Event::GetStatus) {
  NanScope();
  Event *event = ObjectWrap::Unwrap<Event>(args.This());
  NanReturnValue(JS_INT(event->status));
}

NAN_METHOD(Event::New)
{
  NanScope();
  Event *e = new Event(args.This());
  e->Wrap(args.This());
  registerCLObj(e);
  NanReturnValue(args.This());
}

Event *Event::New(cl_event ew)
{
  NanScope();

  Local<Value> arg = NanNew(0);
  Local<FunctionTemplate> constructorHandle = NanNew(constructor_template);
  Local<Object> obj = constructorHandle->GetFunction()->NewInstance(1, &arg);

  Event *e = ObjectWrap::Unwrap<Event>(obj);
  e->event = ew;

  return e;
}

/********************************************
 *
 * UserEvent
 *
 ********************************************/
 Persistent<FunctionTemplate> UserEvent::constructor_template;

void UserEvent::Init(Handle<Object> target)
{
  NanScope();

  // constructor
  Local<FunctionTemplate> ctor = NanNew<FunctionTemplate>(UserEvent::New);
  NanAssignPersistent(constructor_template, ctor);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(JS_STR("WebCLUserEvent"));

  // prototype
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getInfo", getInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getProfilingInfo", getProfilingInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_setCallback", setCallback);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_setStatus", setStatus);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_release", release);

  Local<ObjectTemplate> proto = ctor->PrototypeTemplate();
  proto->SetAccessor(JS_STR("status"), GetStatus, NULL);

  target->Set(JS_STR("WebCLUserEvent"), ctor->GetFunction());
}

UserEvent::UserEvent(Handle<Object> wrapper) : Event(wrapper)
{
}

NAN_METHOD(UserEvent::release)
{
  return Event::release(args);
}

NAN_METHOD(UserEvent::getInfo)
{
  return Event::getInfo(args);
}

NAN_METHOD(UserEvent::getProfilingInfo)
{
  return Event::getProfilingInfo(args);
}

NAN_METHOD(UserEvent::setStatus)
{
  NanScope();
  UserEvent *e = ObjectWrap::Unwrap<UserEvent>(args.This());
  int status = args[0]->Int32Value();

  cl_int ret=::clSetUserEventStatus(e->getEvent(),status);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(INVALID_EVENT);
    REQ_ERROR_THROW(INVALID_VALUE);
    REQ_ERROR_THROW(INVALID_OPERATION);
    REQ_ERROR_THROW(OUT_OF_RESOURCES);
    REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  e->status=status;

  NanReturnUndefined();
}

NAN_METHOD(UserEvent::setCallback)
{
  return Event::setCallback(args); 
}

NAN_GETTER(UserEvent::GetStatus) {
  return Event::GetStatus(property,args);
}

NAN_METHOD(UserEvent::New)
{
  NanScope();
  UserEvent *e = new UserEvent(args.This());
  e->Wrap(args.This());
  registerCLObj(e);
  NanReturnValue(args.This());
}

UserEvent *UserEvent::New(cl_event ew)
{
  NanScope();

  Local<Value> arg = NanNew(0);
  Local<FunctionTemplate> constructorHandle = NanNew(constructor_template);
  Local<Object> obj = constructorHandle->GetFunction()->NewInstance(1, &arg);

  UserEvent *e = ObjectWrap::Unwrap<UserEvent>(obj);
  e->event = ew;

  return e;
}


} // namespace
