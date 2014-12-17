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

#include "program.h"
#include "device.h"
#include "kernel.h"
#include "context.h"

#include <vector>
#include <cstdlib>
#include <cstring>

using namespace v8;

namespace webcl {

Persistent<FunctionTemplate> Program::constructor_template;

void Program::Init(Handle<Object> target)
{
  NanScope();

  // constructor
  Local<FunctionTemplate> ctor = NanNew<FunctionTemplate>(Program::New);
  NanAssignPersistent(constructor_template, ctor);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(NanNew("WebCLProgram"));

  // prototype
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getInfo", getInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_getBuildInfo", getBuildInfo);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_build", build);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_createKernel", createKernel);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_createKernelsInProgram", createKernelsInProgram);
  NODE_SET_PROTOTYPE_METHOD(ctor, "_release", release);

  target->Set(NanNew("WebCLProgram"), ctor->GetFunction());
}

Program::Program(Handle<Object> wrapper) : program(0)
{
  _type=CLObjType::Program;
}

void Program::Destructor() {
  #ifdef LOGGING
  cout<<"  Destroying CL program"<<endl;
  #endif
  if(program) ::clReleaseProgram(program);
  program=0;
}

NAN_METHOD(Program::release)
{
  NanScope();
  Program *prog = ObjectWrap::Unwrap<Program>(args.This());
  
  DESTROY_WEBCL_OBJECT(prog);
  
  NanReturnUndefined();
}

NAN_METHOD(Program::getInfo)
{
  NanScope();
  Program *prog = ObjectWrap::Unwrap<Program>(args.This());
  cl_program_info param_name = args[0]->Uint32Value();

  switch (param_name) {
  case CL_PROGRAM_REFERENCE_COUNT:
  case CL_PROGRAM_NUM_DEVICES: {
    cl_uint value=0;
    cl_int ret=::clGetProgramInfo(prog->getProgram(), param_name, sizeof(cl_uint), &value, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }
    NanReturnValue(NanNew(value));
  }
  case CL_PROGRAM_CONTEXT: {
    cl_context value=NULL;
    cl_int ret=::clGetProgramInfo(prog->getProgram(), param_name, sizeof(cl_context), &value, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }
    if(value) {
      WebCLObject *obj=findCLObj((void*)value);
      if(obj) {
        //::clRetainContext(value);
        NanReturnValue(NanObjectWrapHandle(obj));
      }
      else
        NanReturnValue(NanObjectWrapHandle(Context::New(value)));
    }
    NanReturnUndefined();
  }
  case CL_PROGRAM_DEVICES: {
    size_t num_devices=0;
    cl_int ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_DEVICES, 0, NULL, &num_devices);
    cl_device_id *devices=new cl_device_id[num_devices];
    ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_DEVICES, sizeof(cl_device_id)*num_devices, devices, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }
    Local<Array> deviceArray = NanNew<Array>(num_devices);
    for (size_t i=0; i<num_devices; i++) {
      cl_device_id d = devices[i];
      WebCLObject *obj=findCLObj((void*)d);
      if(obj) {
        //::clRetainDevice(d);
        deviceArray->Set(i, NanObjectWrapHandle(obj));
      }
      else
        deviceArray->Set(i, NanObjectWrapHandle(Device::New(d)));
    }
    delete[] devices;
    NanReturnValue(deviceArray);
  }
  case CL_PROGRAM_SOURCE: {
    size_t size=0;
    cl_int ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_SOURCE, 0, NULL, &size);
    char *source=new char[size];
    ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_SOURCE, sizeof(char)*size, source, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }
    Local<String> str=NanNew(source, (int) size-1);
    delete[] source;
    NanReturnValue(str);
  }
  case CL_PROGRAM_BINARY_SIZES: {
    size_t nsizes=0;
    cl_int ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_BINARY_SIZES, 0, NULL, &nsizes);
    size_t *sizes=new size_t[nsizes];
    ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_BINARY_SIZES, sizeof(size_t)*nsizes, sizes, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }
    Local<Array> sizesArray = NanNew<Array>(nsizes);
    for (size_t i=0; i<nsizes; i++) {
      sizesArray->Set(i, JS_INT(sizes[i]));
    }
    delete[] sizes;
    NanReturnValue(sizesArray);
  }
  case CL_PROGRAM_BINARIES: { // TODO
    return NanThrowError("PROGRAM_BINARIES not implemented");

    size_t nbins=0;
    cl_int ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_BINARIES, 0, NULL, &nbins);
    char* *binaries=new char*[nbins];
    ret=::clGetProgramInfo(prog->getProgram(), CL_PROGRAM_BINARIES, sizeof(char*)*nbins, binaries, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }

    // TODO create an array for Uint8Array to return each binary associated to each device
    // Handle<Value> abv = Context::GetCurrent()->Global()->Get(NanNewSymbol("ArrayBuffer"));
    // Handle<Value> argv[] = { NanNew(size) };
    // Handle<Object> arrbuf = Handle<Function>::Cast(abv)->NewInstance(1, argv);
    // void *buffer = arrbuf->GetPointerFromInternalField(0);
    // memcpy(buffer, data, size);

    delete[] binaries;
    NanReturnUndefined();
  }
  default:
    return NanThrowError("UNKNOWN param_name");
  }
}

NAN_METHOD(Program::getBuildInfo)
{
  NanScope();
  Program *prog = ObjectWrap::Unwrap<Program>(args.This());
  Device *dev = ObjectWrap::Unwrap<Device>(args[0]->ToObject());
  cl_program_info param_name = args[1]->Uint32Value();

  switch (param_name) {
  case CL_PROGRAM_BUILD_STATUS: {
    cl_build_status param_value;
    cl_int ret=::clGetProgramBuildInfo(prog->getProgram(), dev->getDevice(), param_name, sizeof(cl_build_status), &param_value, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_DEVICE);
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }
    NanReturnValue(JS_INT(param_value));
  }
  default: {
    size_t param_value_size_ret=0;
    cl_int ret=::clGetProgramBuildInfo(prog->getProgram(), dev->getDevice(), param_name, 0,
        NULL, &param_value_size_ret);
    char *param_value=new char[param_value_size_ret];
    ret=::clGetProgramBuildInfo(prog->getProgram(), dev->getDevice(), param_name, param_value_size_ret,
        param_value, NULL);
    if (ret != CL_SUCCESS) {
      REQ_ERROR_THROW(INVALID_DEVICE);
      REQ_ERROR_THROW(INVALID_VALUE);
      REQ_ERROR_THROW(INVALID_PROGRAM);
      REQ_ERROR_THROW(OUT_OF_RESOURCES);
      REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
      return NanThrowError("UNKNOWN ERROR");
    }
    Local<Value> obj = JS_STR(param_value,(int)param_value_size_ret);
    delete[] param_value;
    NanReturnValue(obj);
  }
  }
}

class ProgramWorker : public NanAsyncWorker {
 public:
  ProgramWorker(Baton *baton)
    : NanAsyncWorker(baton->callback), baton_(baton) {
    }

  ~ProgramWorker() {
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
    // printf("[build] execute\n");
  }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    NanScope();

    Local<Value> argv[]={
        JS_INT(baton_->error)
    };

    // printf("[build] callback JS\n");
    callback->Call(1, argv);
  }

  private:
    Baton *baton_;
};

void Program::callback (cl_program program, void *user_data)
{
  //cout<<"[Program::driver_callback] thread "<<pthread_self()<<endl<<flush;
  Baton *baton = static_cast<Baton*>(user_data);
  //cout<<"  baton: "<<hex<<baton<<dec<<endl<<flush;
  baton->error=0;

  int num_devices=0;
  ::clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES, sizeof(int), &num_devices, NULL);
  if(num_devices>0) {
    cl_device_id *devices=new cl_device_id[num_devices];
    ::clGetProgramInfo(program, CL_PROGRAM_DEVICES, sizeof(cl_device_id)*num_devices, devices, NULL);
    for(int i=0;i<num_devices;i++) {
      int err=CL_SUCCESS;
      ::clGetProgramBuildInfo(program, devices[i], CL_PROGRAM_BUILD_STATUS, sizeof(int), &err, NULL);
      baton->error |= err;
    }
    delete[] devices;
  }

  // printf("[build] calling async JS cb\n");
  NanAsyncQueueWorker(new ProgramWorker(baton));
}

NAN_METHOD(Program::build)
{
  NanScope();
  Program *prog = ObjectWrap::Unwrap<Program>(args.This());

  cl_device_id *devices=NULL;
  int num=0;
  if(args[0]->IsArray()) {
    Local<Array> deviceArray = Local<Array>::Cast(args[0]);
    //cout<<"Building program for "<<deviceArray->Length()<<" devices"<<endl;
    num=deviceArray->Length();
    devices=new cl_device_id[num];
    for (int i=0; i<num; i++) {
      Local<Object> obj = deviceArray->Get(i)->ToObject();
      Device *d = ObjectWrap::Unwrap<Device>(obj);
      //cout<<"Device "<<i<<": "<<d->getDevice()<<endl;
      devices[i] = d->getDevice();
    }
  }
  else if(args[0]->IsObject()) {
    Local<Object> obj = args[0]->ToObject();
    Device *d = ObjectWrap::Unwrap<Device>(obj);
    num=1;
    devices=new cl_device_id;
    *devices= d->getDevice();
  }
  //cout<<"[Program::build] #devices: "<<num<<" ptr="<<hex<<devices<<dec<<endl<<flush;

  char *options=NULL;
  if(!args[1]->IsUndefined() && !args[1]->IsNull() && args[1]->IsString()) {
    String::Utf8Value str(args[1]);
    //cout<<"str length: "<<str.length()<<endl;
    if(str.length()>0) {
      options = ::strdup(*str);
    }
  }

  Baton *baton=NULL;
  if(args.Length()==3 && !args[2]->IsUndefined() && args[2]->IsFunction()) {

    baton=new Baton();
    baton->callback=new NanCallback(args[2].As<Function>());
  }

  // printf("Build program with baton %p\n",baton);

  cl_int ret = ::clBuildProgram(prog->getProgram(), num, devices,
      options,
      baton ? Program::callback : NULL,
      baton);

  if(options) free(options);
  if(devices) delete[] devices;

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(INVALID_PROGRAM);
    REQ_ERROR_THROW(INVALID_VALUE);
    REQ_ERROR_THROW(INVALID_DEVICE);
    REQ_ERROR_THROW(INVALID_BINARY);
    REQ_ERROR_THROW(INVALID_BUILD_OPTIONS);
    REQ_ERROR_THROW(INVALID_OPERATION);
    REQ_ERROR_THROW(COMPILER_NOT_AVAILABLE);
    REQ_ERROR_THROW(BUILD_PROGRAM_FAILURE);
    REQ_ERROR_THROW(OUT_OF_RESOURCES);
    REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  NanReturnUndefined();
}

NAN_METHOD(Program::createKernel)
{
  NanScope();
  Program *prog = ObjectWrap::Unwrap<Program>(args.This());

  Local<String> str = args[0]->ToString();
  String::Utf8Value astr(str);

  cl_int ret = CL_SUCCESS;
  cl_kernel kw = ::clCreateKernel(prog->getProgram(), (const char*) *astr, &ret);

  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(INVALID_PROGRAM);
    REQ_ERROR_THROW(INVALID_PROGRAM_EXECUTABLE);
    REQ_ERROR_THROW(INVALID_KERNEL_NAME);
    REQ_ERROR_THROW(INVALID_KERNEL_DEFINITION);
    REQ_ERROR_THROW(INVALID_VALUE);
    REQ_ERROR_THROW(OUT_OF_RESOURCES);
    REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  NanReturnValue(NanObjectWrapHandle(Kernel::New(kw)));
}

NAN_METHOD(Program::createKernelsInProgram)
{
  NanScope();
  Program *prog = ObjectWrap::Unwrap<Program>(args.This());

  Local<String> str = args[0]->ToString();
  String::Utf8Value astr(str);

  cl_uint num_kernels=0;
  cl_kernel *kernels=NULL;
  cl_int ret = ::clCreateKernelsInProgram(prog->getProgram(), 0, NULL, &num_kernels);

  if(ret == CL_SUCCESS && num_kernels>0) {
    kernels=new cl_kernel[num_kernels];
    ret = ::clCreateKernelsInProgram(prog->getProgram(), num_kernels, kernels, NULL);
  }

  if (ret != CL_SUCCESS) {
    delete[] kernels;
    REQ_ERROR_THROW(INVALID_PROGRAM);
    REQ_ERROR_THROW(INVALID_PROGRAM_EXECUTABLE);
    REQ_ERROR_THROW(INVALID_VALUE);
    REQ_ERROR_THROW(OUT_OF_RESOURCES);
    REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }

  // build list of WebCLKernels
  Local<Array> jsKernels=NanNew<Array>(num_kernels);

  for(cl_uint i=0;i<num_kernels;i++) {
    jsKernels->Set(i, NanObjectWrapHandle( Kernel::New( kernels[i] ) ) );
  }

  delete[] kernels;
  NanReturnValue(jsKernels);
}

NAN_METHOD(Program::New)
{
  if (!args.IsConstructCall())
    return NanThrowTypeError("Constructor cannot be called as a function.");

  NanScope();
  /*Context *context = ObjectWrap::Unwrap<Context>(args[0]->ToObject());

  Local<String> str = args[1]->ToString();
  String::Utf8Value astr(str);
  cl::Program::Sources sources;
  std::pair<const char*, ::size_t> source(*astr,astr.length());
  sources.push_back(source);

  cl_int ret = CL_SUCCESS;
  cl::Program *pw = new cl::Program(*context->getContext(),sources,&ret);
  if (ret != CL_SUCCESS) {
    REQ_ERROR_THROW(INVALID_CONTEXT);
    REQ_ERROR_THROW(INVALID_VALUE);
    REQ_ERROR_THROW(OUT_OF_RESOURCES);
    REQ_ERROR_THROW(OUT_OF_HOST_MEMORY);
    return NanThrowError("UNKNOWN ERROR");
  }*/

  Program *p = new Program(args.This());
  //p->program=pw;
  p->Wrap(args.This());
  registerCLObj(p);
  NanReturnValue(args.This());
}

Program *Program::New(cl_program pw)
{
  NanScope();

  Local<Value> arg = NanNew(0);
  Local<FunctionTemplate> constructorHandle = NanNew(constructor_template);
  Local<Object> obj = constructorHandle->GetFunction()->NewInstance(1, &arg);

  Program *progobj = ObjectWrap::Unwrap<Program>(obj);
  progobj->program = pw;

  return progobj;
}

} // namespace
