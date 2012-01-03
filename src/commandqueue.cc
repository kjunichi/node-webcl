/*
** This file contains proprietary software owned by Motorola Mobility, Inc. **
** No rights, expressed or implied, whatsoever to this software are provided by Motorola Mobility, Inc. hereunder. **
**
** (c) Copyright 2011 Motorola Mobility, Inc.  All Rights Reserved.  **
*/

#include "commandqueue.h"
#include "platform.h"
#include "context.h"
#include "device.h"
#include "memoryobject.h"
#include "event.h"
#include "kernel.h"
#include "mappedregion.h"
#include <vector>
#include <node_buffer.h>

#include <iostream>
using namespace std;

using namespace v8;
using namespace node;

namespace webcl {

// TODO handle event_wait_list and event for all methods
/*
#define MakeEventWaitList(arg) \
  vector<cl_event> *pevents=NULL;\
  vector<cl_event> events;\
  if(!(arg)->IsUndefined()) {\
    Local<Array> eventWaitArray = Array::Cast(*(arg));\
    if(eventWaitArray->Length()>0) {\
      pevents=&events;\
      for (int i=0; i<eventWaitArray->Length(); i++) {\
        Local<Object> obj = eventWaitArray->Get(i)->ToObject();\
        Event *e = ObjectWrap::Unwrap<Event>(obj);\
        events.push_back( e->getEvent() );\
      }\
    }\
  }
*/

Persistent<FunctionTemplate> CommandQueue::constructor_template;

// e.g. IsUint32Array(v)
#define IS_BUFFER_FUNC(name, type)\
    bool Is##name(v8::Handle<v8::Value> val) {\
  if (!val->IsObject()) return false;\
  v8::Local<v8::Object> obj = val->ToObject();\
  if (obj->GetIndexedPropertiesExternalArrayDataType() == type)\
  return true;\
  return false;\
}

IS_BUFFER_FUNC(Int8Array, kExternalByteArray);
IS_BUFFER_FUNC(Uint8Array, kExternalUnsignedByteArray);
IS_BUFFER_FUNC(Int16Array, kExternalShortArray);
IS_BUFFER_FUNC(Uint16Array, kExternalUnsignedShortArray);
IS_BUFFER_FUNC(Int32Array, kExternalIntArray);
IS_BUFFER_FUNC(Uint32Array, kExternalUnsignedIntArray);
IS_BUFFER_FUNC(Float32Array, kExternalFloatArray);

void CommandQueue::Init(Handle<Object> target)
{
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(CommandQueue::New);
  constructor_template = Persistent<FunctionTemplate>::New(t);

  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
  constructor_template->SetClassName(String::NewSymbol("CommandQueue"));

  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_getCommandQueueInfo", getCommandQueueInfo);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueNDRangeKernel", enqueueNDRangeKernel);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueTask", enqueueTask);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueWriteBuffer", enqueueWriteBuffer);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueReadBuffer", enqueueReadBuffer);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueCopyBuffer", enqueueCopyBuffer);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueWriteBufferRect", enqueueWriteBufferRect);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueReadBufferRect", enqueueReadBufferRect);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueCopyBufferRect", enqueueCopyBufferRect);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueWriteImage", enqueueWriteImage);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueReadImage", enqueueReadImage);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueCopyImage", enqueueCopyImage);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueCopyImageToBuffer", enqueueCopyImageToBuffer);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueCopyBufferToImage", enqueueCopyBufferToImage);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueMapBuffer", enqueueMapBuffer);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueMapImage", enqueueMapImage);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueUnmapMemObject", enqueueUnmapMemObject);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueMarker", enqueueMarker);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueWaitForEvents", enqueueWaitForEvents);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_enqueueBarrier", enqueueBarrier);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_flush", flush);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "_finish", finish);

  target->Set(String::NewSymbol("CommandQueue"), constructor_template->GetFunction());
}

CommandQueue::CommandQueue(Handle<Object> wrapper) : command_queue(0)
{
}

void CommandQueue::Destructor() {
  cout<<"Destroying CL command queue"<<endl;
  if(command_queue) ::clReleaseCommandQueue(command_queue);
  command_queue=0;
}

JS_METHOD(CommandQueue::getCommandQueueInfo)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  cl_command_queue_info param_name = args[0]->Uint32Value();

  switch (param_name) {
  case CL_QUEUE_CONTEXT: {
    cl_context ctx=0;
    cl_int ret=::clGetCommandQueueInfo(cq->getCommandQueue(), param_name, sizeof(cl_context), &ctx, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
      REQ_ERROR_THROW(CL_INVALID_VALUE);
      REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
      REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
      return ThrowError("UNKNOWN ERROR");
    }
    return scope.Close(Context::New(ctx)->handle_);
  }
  case CL_QUEUE_DEVICE: {
    cl_device_id dev=0;
    cl_int ret=::clGetCommandQueueInfo(cq->getCommandQueue(), param_name, sizeof(cl_device_id), &dev, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
      REQ_ERROR_THROW(CL_INVALID_VALUE);
      REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
      REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
      return ThrowError("UNKNOWN ERROR");
    }
    return scope.Close(Device::New(dev)->handle_);
  }
  case CL_QUEUE_REFERENCE_COUNT:
  case CL_QUEUE_PROPERTIES: {
    cl_uint param_value;
    cl_int ret=::clGetCommandQueueInfo(cq->getCommandQueue(), param_name, sizeof(cl_uint), &param_value, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
      REQ_ERROR_THROW(CL_INVALID_VALUE);
      REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
      REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
      return ThrowError("UNKNOWN ERROR");
    }
    return scope.Close(JS_INT(param_value));
  }
  default:
    return ThrowError("CL_INVALID_VALUE");
  }
}

JS_METHOD(CommandQueue::enqueueNDRangeKernel)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  REQ_ARGS(4);

  Kernel *kernel = ObjectWrap::Unwrap<Kernel>(args[0]->ToObject());

  size_t *offsets=NULL;
  cl_uint num_offsets=0;
  if(!args[1]->IsUndefined() && !args[1]->IsNull()) {
    Local<Array> arr = Array::Cast(*args[1]);
    num_offsets=arr->Length();
    offsets=new size_t[num_offsets];
    for(int i=0;i<num_offsets;i++)
      offsets[i]=arr->Get(i)->Uint32Value();
  }

  size_t *globals=NULL;
  cl_uint num_globals=0;
  if(!args[2]->IsUndefined() && !args[2]->IsNull()) {
    Local<Array> arr = Array::Cast(*args[2]);
    num_globals=arr->Length();
    globals=new size_t[num_globals];
    for(int i=0;i<num_globals;i++)
      globals[i]=arr->Get(i)->Uint32Value();
  }

  size_t *locals=NULL;
  cl_uint num_locals=0;
  if(!args[3]->IsUndefined() && !args[3]->IsNull()) {
    Local<Array> arr = Array::Cast(*args[3]);
    num_locals=arr->Length();
    locals=new size_t[num_locals];
    for(int i=0;i<num_locals;i++)
      locals[i]=arr->Get(i)->Uint32Value();
  }

  //MakeEventWaitList(args[4]);
  cl_event event=NULL;
  cout<<"ND Range dimension="<<num_globals<<endl;
  cl_int ret=::clEnqueueNDRangeKernel(
      cq->getCommandQueue(), kernel->getKernel(),
      num_globals, // work dimension
      offsets,
      globals,
      locals,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL); //&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_PROGRAM_EXECUTABLE);
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_KERNEL);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_KERNEL_ARGS);
    REQ_ERROR_THROW(CL_INVALID_WORK_DIMENSION);
    REQ_ERROR_THROW(CL_INVALID_GLOBAL_OFFSET);
    REQ_ERROR_THROW(CL_INVALID_WORK_GROUP_SIZE);
    REQ_ERROR_THROW(CL_INVALID_WORK_ITEM_SIZE);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueTask)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  REQ_ARGS(1);

  Kernel *k = ObjectWrap::Unwrap<Kernel>(args[0]->ToObject());

  //MakeEventWaitList(args[1]);
  cl_event event=NULL;

  cl_int ret=::clEnqueueTask(
      cq->getCommandQueue(), k->getKernel(),
      0,//(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL,//(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL);//&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_PROGRAM_EXECUTABLE);
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_KERNEL);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_KERNEL_ARGS);
    REQ_ERROR_THROW(CL_INVALID_WORK_GROUP_SIZE);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueWriteBuffer)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());

  // TODO: arg checking
  cl_bool blocking_write = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;


  Local<Array> region=Array::Cast(*args[2]);
  Local<Value> vbuffer=region->Get(JS_STR("buffer"));
  void *ptr=NULL;
  if(!vbuffer->IsUndefined())
    ptr = vbuffer->ToObject()->GetIndexedPropertiesExternalArrayData();

  size_t offset=0;
  Local<Value> voffset=region->Get(JS_STR("offset"));
  if(!voffset->IsUndefined())
    offset=voffset->Uint32Value();

  size_t size=0;
  Local<Value> vsize=region->Get(JS_STR("size"));
  if(!vsize->IsUndefined())
    size=vsize->Uint32Value();

  //MakeEventWaitList(args[3]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueWriteBuffer(
                  cq->getCommandQueue(), mo->getMemory(), blocking_write, offset, size,
                  ptr,
                  0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
                  NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
                  NULL); //&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueReadBuffer)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());

  // TODO: arg checking
  cl_bool blocking_read = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;

  Local<Array> region=Array::Cast(*args[2]);
  Local<Value> vbuffer=region->Get(JS_STR("buffer"));
  void *ptr=NULL;
  if(!vbuffer->IsUndefined())
    ptr = vbuffer->ToObject()->GetIndexedPropertiesExternalArrayData();

  size_t offset=0;
  Local<Value> voffset=region->Get(JS_STR("offset"));
  if(!voffset->IsUndefined())
    offset=voffset->Uint32Value();

  size_t size=0;
  Local<Value> vsize=region->Get(JS_STR("size"));
  if(!vsize->IsUndefined())
    size=vsize->Uint32Value();

  //MakeEventWaitList(args[3]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueReadBuffer(
      cq->getCommandQueue(), mo->getMemory(), blocking_read, offset, size,
      ptr,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL); //&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

// TODO update with regions
JS_METHOD(CommandQueue::enqueueCopyBuffer)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  MemoryObject *mo_src = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  MemoryObject *mo_dst = ObjectWrap::Unwrap<MemoryObject>(args[1]->ToObject());

  // TODO: arg checking
  size_t src_offset = args[2]->NumberValue();
  size_t dst_offset = args[3]->NumberValue();
  size_t size = args[4]->NumberValue();

  //MakeEventWaitList(args[5]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueCopyBuffer(
      cq->getCommandQueue(), mo_src->getMemory(), mo_dst->getMemory(),
      src_offset, dst_offset, size,
      0,//(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL,//(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL);//&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_MEM_COPY_OVERLAP);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueWriteBufferRect)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());

  // TODO: arg checking
  cl_bool blocking_write = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;
  size_t buffer_offset[3];
  size_t host_offset[3];
  size_t region[3];

  Local<Array> bufferOrigin = Array::Cast(*args[2]);
  for (int i=0; i<3; i++) {
    size_t s = bufferOrigin->Get(i)->NumberValue();
    buffer_offset[i] = s;
  }

  Local<Array> hostOrigin = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = hostOrigin->Get(i)->NumberValue();
    host_offset[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[4]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->NumberValue();
    region[i] = s;
  }

  size_t buffer_row_pitch = args[5]->NumberValue();
  size_t buffer_slice_pitch = args[6]->NumberValue();
  size_t host_row_pitch = args[7]->NumberValue();
  size_t host_slice_pitch = args[8]->NumberValue();

  void *ptr = args[9]->ToObject()->GetIndexedPropertiesExternalArrayData();

  //MakeEventWaitList(args[10]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueWriteBufferRect(
      cq->getCommandQueue(),
      mo->getMemory(),
      blocking_write,
      buffer_offset,
      host_offset,
      region,
      buffer_row_pitch,
      buffer_slice_pitch,
      host_row_pitch,
      host_slice_pitch,
      ptr,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL); //&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueReadBufferRect)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());

  // TODO: arg checking
  cl_bool blocking_read = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;
  size_t buffer_offset[3];
  size_t host_offset[3];
  size_t region[3];

  Local<Array> bufferOrigin = Array::Cast(*args[2]);
  for (int i=0; i<3; i++) {
    size_t s = bufferOrigin->Get(i)->Uint32Value();
    buffer_offset[i] = s;
  }

  Local<Array> hostOrigin = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = hostOrigin->Get(i)->Uint32Value();
    host_offset[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[4]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->Uint32Value();
    region[i] = s;
  }

  size_t buffer_row_pitch = args[5]->Uint32Value();
  size_t buffer_slice_pitch = args[6]->Uint32Value();
  size_t host_row_pitch = args[7]->Uint32Value();
  size_t host_slice_pitch = args[8]->Uint32Value();

  // TODO use MappedRegion instead of ptr
  void *ptr = args[9]->ToObject()->GetIndexedPropertiesExternalArrayData();

  //MakeEventWaitList(args[10]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueReadBufferRect(
      cq->getCommandQueue(),
      mo->getMemory(),
      blocking_read,
      buffer_offset,
      host_offset,
      region,
      buffer_row_pitch,
      buffer_slice_pitch,
      host_row_pitch,
      host_slice_pitch,
      ptr,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL); //&event);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueCopyBufferRect)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo_src = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  MemoryObject *mo_dst = ObjectWrap::Unwrap<MemoryObject>(args[1]->ToObject());

  size_t src_origin[3];
  size_t dst_origin[3];
  size_t region[3];

  Local<Array> srcOrigin = Array::Cast(*args[2]);
  for (int i=0; i<3; i++) {
    size_t s = srcOrigin->Get(i)->Uint32Value();
    src_origin[i] = s;
  }

  Local<Array> dstOrigin = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = dstOrigin->Get(i)->Uint32Value();
    dst_origin[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[4]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->Uint32Value();
    region[i] = s;
  }

  size_t src_row_pitch = args[5]->Uint32Value();
  size_t src_slice_pitch = args[6]->Uint32Value();
  size_t dst_row_pitch = args[7]->Uint32Value();
  size_t dst_slice_pitch = args[8]->Uint32Value();

  //MakeEventWaitList(args[10]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueCopyBufferRect(
      cq->getCommandQueue(),
      mo_src->getMemory(),
      mo_dst->getMemory(),
      (const size_t *)src_origin,
      (const size_t *)dst_origin,
      (const size_t *)region,
      src_row_pitch,
      src_slice_pitch,
      dst_row_pitch,
      dst_slice_pitch,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL);//(cl_event*) event);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MEM_COPY_OVERLAP);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueWriteImage)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  cout<<"enqueue image "<<hex<<mo->getMemory()<<dec<<endl;

  // TODO: arg checking
  cl_bool blocking_write = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;
  size_t origin[3]={0,0,0};
  size_t region[3]={1,1,1};

  Local<Array> originArray = Array::Cast(*args[2]);
  for (int i=0; i<originArray->Length(); i++) {
    origin[i] = originArray->Get(i)->NumberValue();
  }

  Local<Array> regionArray = Array::Cast(*args[3]);
  for (int i=0; i<regionArray->Length(); i++) {
    region[i] = regionArray->Get(i)->NumberValue();
  }

  size_t row_pitch = args[4]->NumberValue();
  size_t slice_pitch = args[5]->NumberValue();

  void *ptr = args[6]->ToObject()->GetIndexedPropertiesExternalArrayData();

  //MakeEventWaitList(args[7]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueWriteImage(
      cq->getCommandQueue(), mo->getMemory(), blocking_write,
      origin,
      region,
      row_pitch,
      slice_pitch,
      ptr,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL); //&event);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_INVALID_OPERATION);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueReadImage)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());

  // TODO: arg checking
  cl_bool blocking_read = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;
  size_t origin[3];
  size_t region[3];

  Local<Array> originArray = Array::Cast(*args[2]);
  for (int i=0; i<3; i++) {
    size_t s = originArray->Get(i)->Uint32Value();
    origin[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->Uint32Value();
    region[i] = s;
  }

  size_t row_pitch = args[4]->Uint32Value();
  size_t slice_pitch = args[5]->Uint32Value();

  void *ptr = args[6]->ToObject()->GetIndexedPropertiesExternalArrayData();

  //MakeEventWaitList(args[7]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueReadImage(
      cq->getCommandQueue(), mo->getMemory(), blocking_read,
      origin,
      region,
      row_pitch, slice_pitch, ptr,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL); //&event);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_INVALID_OPERATION);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueCopyImage)
{   
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo_src = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  MemoryObject *mo_dst = ObjectWrap::Unwrap<MemoryObject>(args[1]->ToObject());

  // TODO: arg checking
  size_t src_origin[3];
  size_t dst_origin[3];
  size_t region[3];

  Local<Array> srcOriginArray = Array::Cast(*args[2]);
  for (int i=0; i<3; i++) {
    size_t s = srcOriginArray->Get(i)->NumberValue();
    src_origin[i] = s;
  }

  Local<Array> dstOriginArray = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = dstOriginArray->Get(i)->NumberValue();
    dst_origin[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[4]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->NumberValue();
    region[i] = s;
  }

  //MakeEventWaitList(args[5]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueCopyImage(
      cq->getCommandQueue(), mo_src->getMemory(), mo_dst->getMemory(),
      src_origin,
      dst_origin,
      region,
      0, //(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL, //(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL); //&event);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_IMAGE_FORMAT_MISMATCH);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_INVALID_OPERATION);
    REQ_ERROR_THROW(CL_MEM_COPY_OVERLAP);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueCopyImageToBuffer)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo_src = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  MemoryObject *mo_dst = ObjectWrap::Unwrap<MemoryObject>(args[1]->ToObject());

  // TODO: arg checking
  size_t src_origin[3];
  size_t region[3];

  Local<Array> srcOriginArray = Array::Cast(*args[2]);
  for (int i=0; i<3; i++) {
    size_t s = srcOriginArray->Get(i)->NumberValue();
    src_origin[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->NumberValue();
    region[i] = s;
  }

  size_t dst_offset = args[4]->NumberValue();

  //MakeEventWaitList(args[5]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueCopyImageToBuffer(
      cq->getCommandQueue(), mo_src->getMemory(), mo_dst->getMemory(),
      src_origin,
      region, dst_offset,
      0,//(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL,//(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL);//&event);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_INVALID_OPERATION);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueCopyBufferToImage)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  MemoryObject *mo_src = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  MemoryObject *mo_dst = ObjectWrap::Unwrap<MemoryObject>(args[1]->ToObject());

  // TODO: arg checking
  size_t dst_origin[3];
  size_t region[3];

  size_t src_offset = args[2]->NumberValue();

  Local<Array> dstOriginArray = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = dstOriginArray->Get(i)->NumberValue();
    dst_origin[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[4]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->NumberValue();
    region[i] = s;
  }

  //MakeEventWaitList(args[5]);

  cl_event event=NULL;
  cl_int ret=::clEnqueueCopyBufferToImage(
      cq->getCommandQueue(), mo_src->getMemory(), mo_dst->getMemory(),
      src_offset,
      dst_origin,
      region,
      0,//(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL,//(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL);//&event);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_INVALID_OPERATION);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueMapBuffer)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  // TODO: arg checking
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  cl_bool blocking = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;
  cl_map_flags flags = args[2]->Uint32Value();
  size_t offset = args[3]->Uint32Value();
  size_t size = args[4]->Uint32Value();

  //MakeEventWaitList(args[5]);

  /*Local<Array> region=Array::Cast(*args[0]);
  Local<Value> vbuffer=region->Get(JS_STR("buffer"));
  Memory *mo=NULL;
  if(!vbuffer->IsUndefined())
    mo = ObjectWrap::Unwrap<Memory>(vbuffer->ToObject());

  size_t offset=0;
  Local<Value> voffset=region->Get(JS_STR("offset"));
  if(!voffset->IsUndefined())
    offset=voffset->Uint32Value();

  size_t size=0;
  Local<Value> vsize=region->Get(JS_STR("size"));
  if(!vsize->IsUndefined())
    size=vsize->Uint32Value();

  MakeEventWaitList(args[3]);*/

  cl_int ret=CL_SUCCESS;
  cl_event event=NULL;
  void *result=::clEnqueueMapBuffer(
              cq->getCommandQueue(), mo->getMemory(),
              blocking, flags, offset, size,
              0,//(pevents != NULL) ? (cl_uint) pevents->size() : 0,
              NULL,//(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
              NULL/*&event*/, &ret);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_MISALIGNED_SUB_BUFFER_OFFSET);
    REQ_ERROR_THROW(CL_MAP_FAILURE);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  /*cout<<"enqueueMapBuffer"<<endl;
  for(int i=0;i<size;i++) {
    cout<<(int)((char*)result)[i]<<" ";
  }
  cout<<endl;*/

  _MappedRegion *mapped_region=new _MappedRegion();
  mapped_region->buffer=Buffer::New((char*) result,size);
  mapped_region->mapped_ptr=(ulong) result; // TODO use SetHiddenValue on buffer
  if(event) mapped_region->event=Event::New(event); // TODO check if this event can be correctly used? and deleted?

  return scope.Close(MappedRegion::New(mapped_region)->handle_);
}

JS_METHOD(CommandQueue::enqueueMapImage)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  // TODO: arg checking
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  cl_bool blocking = args[1]->BooleanValue() ? CL_TRUE : CL_FALSE;
  cl_map_flags flags = args[2]->Uint32Value();

  size_t origin[3];
  size_t region[3];

  Local<Array> originArray = Array::Cast(*args[3]);
  for (int i=0; i<3; i++) {
    size_t s = originArray->Get(i)->Uint32Value();
    origin[i] = s;
  }

  Local<Array> regionArray = Array::Cast(*args[4]);
  for (int i=0; i<3; i++) {
    size_t s = regionArray->Get(i)->Uint32Value();
    region[i] = s;
  }

  //MakeEventWaitList(args[5]);

  size_t row_pitch;
  size_t slice_pitch;

  cl_event event=NULL; // TODO should we create an Event
  cl_int ret=CL_SUCCESS;
  void *result=::clEnqueueMapImage(
              cq->getCommandQueue(), mo->getMemory(),
              blocking, flags,
              origin,
              region,
              &row_pitch, &slice_pitch,
              0,//(pevents != NULL) ? (cl_uint) pevents->size() : 0,
              NULL,//(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
              NULL /*&event*/, &ret);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_INVALID_IMAGE_SIZE);
    REQ_ERROR_THROW(CL_MAP_FAILURE);
    REQ_ERROR_THROW(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    REQ_ERROR_THROW(CL_MEM_OBJECT_ALLOCATION_FAILURE);
    REQ_ERROR_THROW(CL_INVALID_OPERATION);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  // TODO: return image_row_pitch, image_slice_pitch?

  size_t nbytes = region[0] * region[1] * region[2];
  return scope.Close(node::Buffer::New((char*)result, nbytes)->handle_);
}

JS_METHOD(CommandQueue::enqueueUnmapMemObject)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  // TODO: arg checking
  MemoryObject *mo = ObjectWrap::Unwrap<MemoryObject>(args[0]->ToObject());
  MappedRegion *region = ObjectWrap::Unwrap<MappedRegion>(args[1]->ToObject());
  char *mapped_ptr=(char*) region->getMappedRegion()->mapped_ptr;

  //cout<<"enqueueUnmapMemObject"<<endl;
  for(int i=0;i<Buffer::Length(region->getMappedRegion()->buffer);i++) {
    mapped_ptr[i]=Buffer::Data(region->getMappedRegion()->buffer)[i];
    //cout<<(int) mapped_ptr[i]<<" ";
  }
  //cout<<endl;

  //MakeEventWaitList(args[2]);

  // TODO should we create the Event or allow user to specify it?
  cl_event event=NULL;
  cl_int ret=::clEnqueueUnmapMemObject(
      cq->getCommandQueue(), mo->getMemory(),
      mapped_ptr,
      0,//(pevents != NULL) ? (cl_uint) pevents->size() : 0,
      NULL,//(pevents != NULL && pevents->size() > 0) ? (cl_event*) &pevents->front() : NULL,
      NULL);//&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_MEM_OBJECT);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_EVENT_WAIT_LIST);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

/* static */
JS_METHOD(CommandQueue::enqueueMarker)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  cl_event event=NULL;
  cl_int ret = ::clEnqueueMarker(cq->getCommandQueue(),&event);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  if(event) return scope.Close(Event::New(event)->handle_);
  return Undefined();
}

JS_METHOD(CommandQueue::enqueueWaitForEvents)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);

  vector<cl_event> event_wait_list;
  Local<Array> eventWaitArray = Array::Cast(*args[0]);
  for (int i=0; i<eventWaitArray->Length(); i++) {
    Local<Object> obj = eventWaitArray->Get(i)->ToObject();
    Event *e = ObjectWrap::Unwrap<Event>(obj);
    event_wait_list.push_back( e->getEvent() );
  }

  cl_int ret = ::clEnqueueWaitForEvents(
      cq->getCommandQueue(),
      event_wait_list.size(),
      &event_wait_list.front());

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_INVALID_VALUE);
    REQ_ERROR_THROW(CL_INVALID_CONTEXT);
    REQ_ERROR_THROW(CL_INVALID_EVENT);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  return Undefined();
}

JS_METHOD(CommandQueue::enqueueBarrier)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  cl_int ret = ::clEnqueueBarrier(cq->getCommandQueue());

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  return Undefined();
}

JS_METHOD(CommandQueue::finish)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  cl_int ret = ::clFinish(cq->getCommandQueue());

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  return Undefined();
}

JS_METHOD(CommandQueue::flush)
{
  HandleScope scope;
  CommandQueue *cq = UnwrapThis<CommandQueue>(args);
  cl_int ret = ::clFlush(cq->getCommandQueue());

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(CL_INVALID_COMMAND_QUEUE);
    REQ_ERROR_THROW(CL_OUT_OF_RESOURCES);
    REQ_ERROR_THROW(CL_OUT_OF_HOST_MEMORY);
    return ThrowError("UNKNOWN ERROR");
  }

  return Undefined();
}

JS_METHOD(CommandQueue::New)
{
  HandleScope scope;
  CommandQueue *cq = new CommandQueue(args.This());
  cq->Wrap(args.This());
  registerCLObj(cq);
  return scope.Close(args.This());
}

CommandQueue *CommandQueue::New(cl_command_queue cw)
{

  HandleScope scope;

  Local<Value> arg = Integer::NewFromUnsigned(0);
  Local<Object> obj = constructor_template->GetFunction()->NewInstance(1, &arg);

  CommandQueue *commandqueue = ObjectWrap::Unwrap<CommandQueue>(obj);
  commandqueue->command_queue = cw;

  return commandqueue;
}

}