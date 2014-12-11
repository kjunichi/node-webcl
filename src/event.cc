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
  Local<FunctionTemplate> ctor = FunctionTemplate::New(v8::Isolate::GetCurrent(),Event::New);
  NanAssignPersistent(constructor_template, ctor);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(JS_STR("WebCLEvent"));

  // prototype
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getInfo", getInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getProfilingInfo", getProfilingInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_setUserEventStatus", setUserEventStatus);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_setCallback", setCallback);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_release", release);

  // attributes
  Local<ObjectTemplate> proto = ctor->PrototypeTemplate();
  proto->SetAccessor(JS_STR("status"), GetStatus, NULL);
  proto->SetAccessor(JS_STR("buffer"), GetBuffer, NULL);

  target->Set(JS_STR("WebCLEvent"), ctor->GetFunction());
}

Event::Event(Handle<Object> wrapper) : /*callback(NULL),*/ event(0), status(0)
{
}

void Event::Destructor()
{
  if(event) {
#ifdef LOGGING
    cout<<"  Destroying CL event "<<event<<endl;//<<" thread: 0x"<<hex<<pthread_self()<<dec<<endl;
#endif
    ::clReleaseEvent(event);
  }
  event=0;
}

NAN_METHOD(Event::release)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());
  
  DESTROY_WEBCL_OBJECT(e);
  
  NanReturnUndefined();
}

NAN_METHOD(Event::getInfo)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());
  cl_event_info param_name = args[0]->Uint32Value();

  switch (param_name) {
  case CL_EVENT_CONTEXT:{
    cl_context param_value=NULL;
    ::clGetEventInfo(e->getEvent(), param_name, sizeof(cl_context), &param_value, NULL);
    NanReturnValue(NanObjectWrapHandle(Context::New(param_value)));
  }
  case CL_EVENT_COMMAND_QUEUE:{
    cl_command_queue param_value=NULL;
    ::clGetEventInfo(e->getEvent(), param_name, sizeof(cl_command_queue), &param_value, NULL);
    NanReturnValue(NanObjectWrapHandle(CommandQueue::New(param_value)));
  }
  case CL_EVENT_REFERENCE_COUNT:
  case CL_EVENT_COMMAND_TYPE:
  case CL_EVENT_COMMAND_EXECUTION_STATUS: {
    cl_uint param_value=0;
    ::clGetEventInfo(e->getEvent(), param_name, sizeof(cl_uint), &param_value, NULL);
    NanReturnValue(Integer::NewFromUnsigned(v8::Isolate::GetCurrent(),param_value));
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

  switch (param_name) {
  case CL_PROFILING_COMMAND_QUEUED:
  case CL_PROFILING_COMMAND_SUBMIT:
  case CL_PROFILING_COMMAND_START:
  case CL_PROFILING_COMMAND_END: {
    cl_ulong param_value=0;
    ::clGetEventProfilingInfo(e->getEvent(), param_name, sizeof(cl_ulong), &param_value, NULL);
    NanReturnValue(JS_INT((int32_t)param_value));
  }
  default:
    return NanThrowError("UNKNOWN param_name");
  }
}

NAN_METHOD(Event::setUserEventStatus)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());

  cl_int ret=::clSetUserEventStatus(e->getEvent(),args[0]->Int32Value());
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_EVENT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_OPERATION);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  NanReturnUndefined();
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
    Local<Value> argv[] = {
      NanNew(baton_->parent),  // event
      NanNew(baton_->data)     // user's message
    };

    // printf("[async event] callback JS\n");
    callback->Call(2, argv);
  }

  private:
    Baton *baton_;
};

void CL_CALLBACK Event::callback (cl_event event, cl_int event_command_exec_status, void *user_data)
{
  // printf("[Event::callback] event=%p, exec status=%d\n",event,event_command_exec_status);
  Baton *baton = static_cast<Baton*>(user_data);
  baton->error = event_command_exec_status;

  NanAsyncQueueWorker(new EventWorker(baton));
}

NAN_METHOD(Event::setCallback)
{
  NanScope();
  Event *e = ObjectWrap::Unwrap<Event>(args.This());
  cl_int command_exec_callback_type = args[0]->Int32Value();
  Local<Value> data=args[2];

  Baton *baton=new Baton();
  NanAssignPersistent(baton->data, data);
  NanAssignPersistent(baton->parent, NanObjectWrapHandle(e));
  baton->callback=new NanCallback(args[1].As<Function>());

  // printf("SetEventCallback event=%p for callback %p\n",e->getEvent(), baton->callback);
  cl_int ret=::clSetEventCallback(e->getEvent(), command_exec_callback_type, callback, baton);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_EVENT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  NanReturnUndefined();
}

NAN_GETTER(Event::GetStatus) {
  NanScope();
  Event *event = ObjectWrap::Unwrap<Event>(args.This() /*Holder()*/);
  NanReturnValue(JS_INT(event->status));
}

// TODO buffer can only be set by enqueueReadBuffer/ReadBufferRect/Image
// TODO update callback to return the event object, not the status
NAN_GETTER(Event::GetBuffer) {
  NanScope();
  //Event *event = ObjectWrap::Unwrap<Event>(args.Holder());
  NanReturnUndefined();
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

  Local<Value> arg = Integer::NewFromUnsigned(v8::Isolate::GetCurrent(),0);
  Local<FunctionTemplate> constructorHandle = NanNew(constructor_template);
  Local<Object> obj = constructorHandle->GetFunction()->NewInstance(1, &arg);

  Event *e = ObjectWrap::Unwrap<Event>(obj);
  e->event = ew;

  return e;
}

} // namespace
