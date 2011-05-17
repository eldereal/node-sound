// 
//  sound.cc
//  node_sound
//  
//  Created by Jorge@jorgechamorro.com on 2011-05-11.
//  Copyright 2011 Proyectos Equis Ka. All rights reserved.
// 

/*
  TODO 
  - Throw exceptions instead of just writing ERRORs to stderr.
  - provide a vumeter per sound
  - play callbacks (DONE)
  - bufferifySync(path) returns a buffer;
  - bufferify(path, cb) renders it in a background thread and calls cb(err, buffer) when done.
*/

#include <v8.h>
#include <node.h>
#include <node_buffer.h>
#include <ev.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#if defined (__APPLE__)
  #include <AudioToolbox/AudioToolbox.h>
  #include <CoreFoundation/CoreFoundation.h>
  
  typedef struct playerStruct {
    
    int paused;
    long int id;
    int playing;
    int destroying;
    unsigned long loop;
    ssize_t bufferLength;
    
    AudioQueueRef AQ;
    UInt32 AQBuffer1Length;
    UInt32 AQBuffer2Length;
    AudioQueueBufferRef AQBuffer1;
    AudioQueueBufferRef AQBuffer2;
    AudioStreamBasicDescription* format;
    
    int hasCallback;
    int callbackIsPending;
    v8::Persistent<v8::Object> pendingJSCallback;
    v8::Persistent<v8::Object> JSObject;
    v8::Persistent<v8::Object> JSCallback;
    
    ExtAudioFileRef inputAudioFile;
    
  } playerStruct;
  
  static AudioStreamBasicDescription gFormato;
  static playerStruct* fondoSnd;
  
#endif

//using namespace node;
//using namespace v8;

typedef struct bufferStruct {
  void* buffer;
  ssize_t used;
  ssize_t size;
} bufferStruct;

typedef struct queueStruct {
  void* item;
  queueStruct* next;
  queueStruct* last;
} queueStruct;
static queueStruct* callbacksQueue= NULL;
static ev_async eio_sound_async_notifier;
pthread_mutex_t callbacksQueue_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct renderJob {
  char* str;
  ssize_t strLen;
  queueStruct* qHead;
  ssize_t bytesRead;
  v8::Persistent<v8::Object> JSCallback;
} renderJob;

static v8::Persistent<v8::String> volume_symbol;
static v8::Persistent<v8::String> loop_symbol;
static v8::Persistent<v8::String> play_symbol;
static v8::Persistent<v8::String> pause_symbol;
static v8::Persistent<v8::Function> volume_function;
static v8::Persistent<v8::Function> loop_function;
static v8::Persistent<v8::Function> play_function;
static v8::Persistent<v8::Function> pause_function;

static v8::Persistent<v8::String> id_symbol;
static v8::Persistent<v8::String> data_symbol;
static v8::Persistent<v8::String> hiddenPlayerPtr_symbol;

static long int createdCtr= 0;
static long int destroyedCtr= 0;
static long int wasPlayingCtr= 0;
static long int playingNow= 0;





// ==================
// = newQueueItem() =
// ==================

queueStruct* newQueueItem (void* item, queueStruct* qHead) {
  queueStruct* nuItem= (queueStruct*) calloc(1, sizeof(queueStruct));
  nuItem->item= item;
  if (qHead) {
    qHead->last->next= nuItem;
    qHead->last= nuItem;
  }
  else {
    nuItem->last= nuItem;
  }
  return nuItem;
}





// ======================
// = destroyQueueItem() =
// ======================

queueStruct* destroyQueueItem (queueStruct* item) {
  queueStruct* next= item->next;
  free(item);
  return next;
}






// ===============
// = newBuffer() =
// ===============

bufferStruct* newBuffer (ssize_t size) {
  bufferStruct* nuBuffer= (bufferStruct*) malloc(sizeof(bufferStruct));
  nuBuffer->buffer= malloc(size);
  nuBuffer->used= 0;
  nuBuffer->size= size;
  v8::V8::AdjustAmountOfExternalAllocatedMemory((int) (size + sizeof(bufferStruct)));
  return nuBuffer;
}





// ===================
// = destroyBuffer() =
// ===================

void destroyBuffer (bufferStruct* buffer) {
  v8::V8::AdjustAmountOfExternalAllocatedMemory((int) -(buffer->size + sizeof(bufferStruct)));
  free(buffer->buffer);
  free(buffer);
}






// =============
// = tracker() =
// =============

void tracker (int i) {
  playingNow+= i;
  
  if (playingNow > 0) {
    if (fondoSnd->playing) {
      fondoSnd->playing= 0;
      AudioQueuePause(fondoSnd->AQ);
      //fprintf(stderr, "\nfondoSnd->pause()");
    }
  }
  else {
      if (!fondoSnd->playing) {
      fondoSnd->playing= 1;
      AudioQueueStart(fondoSnd->AQ, NULL);
      //fprintf(stderr, "\nfondoSnd->play()");
    }
  }
}







// ===============
// = newPlayer() =
// ===============

playerStruct* newPlayer () {
  playerStruct* player;
  
  player= (playerStruct*) calloc(1, sizeof(playerStruct));
  v8::V8::AdjustAmountOfExternalAllocatedMemory(sizeof(playerStruct));
  
  player->id= createdCtr++;
  player->format= &gFormato;
  player->loop= 0;
  player->paused= 0;
  player->playing= 0;
  player->destroying= 0;
  player->hasCallback= 0;
  player->callbackIsPending= 0;
  
  return player;
}







// ====================
// = **** Volume **** =
// ====================

v8::Handle<v8::Value> Volume (const v8::Arguments &args) {
  
  v8::HandleScope scope;
  
#if defined (__APPLE__)
  
  OSStatus err;
  double volumen;
  
  playerStruct* player;
  player= (playerStruct*) (v8::External::Unwrap(args.This()->ToObject()->GetHiddenValue(hiddenPlayerPtr_symbol)));
  
  if (args.Length() && args[0]->IsNumber()) {
    volumen= args[0]->NumberValue();
    
    if (volumen < 0) volumen= 0;
    else if (volumen > 1) volumen= 1;
    
    err= AudioQueueSetParameter (player->AQ, kAudioQueueParam_Volume, (AudioQueueParameterValue) volumen);
  }
  
#else
  fprintf(stderr, "\nERROR *** Sound::volume() LINUX not implemented argghh");
  fflush(stderr);
  
#endif


  end:
  return scope.Close(args.This());
}









// ==================
// = **** Loop **** =
// ==================

v8::Handle<v8::Value> Loop (const v8::Arguments &args) {
  
  v8::HandleScope scope;
  
#if defined (__APPLE__)
  
  double loop;
  
  playerStruct* player;
  player= (playerStruct*) (v8::External::Unwrap(args.This()->ToObject()->GetHiddenValue(hiddenPlayerPtr_symbol)));
  
  if (args.Length() && args[0]->IsNumber()) {
    loop= args[0]->NumberValue();
    
    if (loop < 0) loop= 0;
    else if (loop > 9007199254740992) loop= 9007199254740992; // 2^53
    
    player->loop= truncl(loop);
  }
  
#else
  fprintf(stderr, "\nERROR *** Sound::loop() LINUX not implemented argghh");
  fflush(stderr);
  
#endif


  end:
  return scope.Close(args.This());
}










// ==================
// = **** Play **** =
// ==================

v8::Handle<v8::Value> Play (const v8::Arguments &args) {
  
  v8::HandleScope scope;
  
#if defined (__APPLE__)
  
  OSStatus err;
  playerStruct* player;
  
  player= (playerStruct*) (v8::External::Unwrap(args.This()->GetHiddenValue(hiddenPlayerPtr_symbol)));
  
  //fprintf(stderr, "\n*** Sound::play() [%ld]", player->id);
  
  if (player->hasCallback) {
    player->hasCallback= 0;
    player->JSCallback.Dispose();
  }
  
  
  if (args.Length()) {
    if (args[0]->IsFunction()) {
      player->hasCallback= 1;
      player->JSCallback= v8::Persistent<v8::Object>::New(args[0]->ToObject());
    }
    else {
      return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::Play(): The callback must be a function")));
    }
  }
  
  
  if (player->paused) {
    player->paused= 0;
    player->playing= 1;
    tracker(+1);
    
    err= AudioQueueStart(player->AQ, NULL);
    if (err) {
      fprintf(stderr, " ERROR:AudioQueueStart:[%d]", err);
    }
    
    goto end;
  }
  
  if (player->playing) goto end;
  
  //fprintf(stderr, "\n[%d] PLAY", player->id);
  //fflush(stderr);
  
  player->playing= 1;
  tracker(+1);
  
  player->AQBuffer1->mAudioDataByteSize= player->AQBuffer1Length;
  err= AudioQueueEnqueueBuffer(player->AQ, player->AQBuffer1, 0, NULL);
  if (err) {
    fprintf(stderr, " ERROR:AudioQueueEnqueueBuffer:[%d]", err);
  }
  
  player->AQBuffer2->mAudioDataByteSize= player->AQBuffer2Length;
  err= AudioQueueEnqueueBuffer(player->AQ, player->AQBuffer2, 0, NULL);
  if (err) {
    fprintf(stderr, " ERROR:AudioQueueEnqueueBuffer:[%d]", err);
  }
  
  err= AudioQueueStart(player->AQ, NULL);
  if (err) {
    fprintf(stderr, " ERROR:AudioQueueStart:[%d]", err);
  }
  
#else
  fprintf(stderr, "\nERROR *** Sound::play() LINUX not implemented argghh");
  fflush(stderr);
  
#endif


  end:
  
  //fflush(stderr);
  return scope.Close(args.This());
}







// ======================================
// = **** AudioQueueBufferCallback **** =
// ======================================

#if defined (__APPLE__)

void AQBufferCallback (void* priv, AudioQueueRef AQ, AudioQueueBufferRef AQBuffer) {
  
  OSStatus err;
  playerStruct* player= (playerStruct*) priv;
  
  //fprintf(stderr, "\n[%ld] AQBufferCallback() [%d]", player->id, 1+(AQBuffer == player->AQBuffer2));
  
  
  if (player->destroying) {
    
    //fprintf(stderr, "\n[%ld] DESTROYING", player->id);
    
    goto end;
  };
  
  if (!player->playing) {
    
    //fprintf(stderr, "\n[%ld] IGNORE", player->id);
    
    goto end;
  }
  
  if (player->loop && (AQBuffer == player->AQBuffer1)) {
    
    //fprintf(stderr, "\n[%ld] loop--", player->id);
    
    player->loop--;
  }
  
  if (player->loop) {
    
    //fprintf(stderr, "\n[%ld] RE_QUEUE[%d]", player->id, 1+(AQBuffer == player->AQBuffer2));
    
    // Si estamos en loop simplemente hay que volver a meterlo en la cola.
    
    AQBuffer->mAudioDataByteSize= (AQBuffer == player->AQBuffer1) ? player->AQBuffer1Length : player->AQBuffer2Length;

    err= AudioQueueEnqueueBuffer(player->AQ, AQBuffer, 0, NULL);
    if (err) {
      fprintf(stderr, " ERROR:AQBufferCallback AudioQueueEnqueueBuffer:[%d] ", err);
    }
    
  }
  else if (AQBuffer == player->AQBuffer2) {
    
    //fprintf(stderr, "\n[%ld] CLEANUP", player->id);
    
    err= AudioQueueStop(player->AQ, true);
    if (err) {
      fprintf(stderr, " ERROR:AudioQueueStop:[%d]", err);
    }
    
    player->playing= 0;
    
    if (player->hasCallback) {
      player->callbackIsPending= 1;
      player->pendingJSCallback= player->JSCallback;
      player->hasCallback= 0;
      queueStruct* theItem= newQueueItem(player, NULL);
      
      pthread_mutex_lock(&callbacksQueue_mutex);
      if (callbacksQueue == NULL) callbacksQueue= theItem;
      else callbacksQueue->last->next= theItem;
      callbacksQueue->last= theItem;
      pthread_mutex_unlock(&callbacksQueue_mutex);
      
      ev_async_send(EV_DEFAULT_UC_ &eio_sound_async_notifier);
    }
  }
  else {
    
    //fprintf(stderr, "\n[%ld] STOP", player->id);
    
    tracker(-1);
    
    err= AudioQueueStop(player->AQ, false);
    if (err) {
      fprintf(stderr, " ERROR:AudioQueueStop:[%d]", err);
    }
  }
  
  end:
  
  //fflush(stderr);
  return;
}

#endif









// ===================
// = **** Pause **** =
// ===================

v8::Handle<v8::Value> Pause (const v8::Arguments &args) {
  
  v8::HandleScope scope;

#if defined (__APPLE__)

  OSStatus err;
  playerStruct* player;
  player= (playerStruct*) (v8::External::Unwrap(args.This()->ToObject()->GetHiddenValue(hiddenPlayerPtr_symbol)));
  
  //fprintf(stderr, "\n*** Sound::pause [%ld]", player->id);
  
  if (player->paused) {
    //fprintf(stderr, " IGNORED_WAS_PAUSED");
    goto end;
  }
  
  if (!player->playing) {
    //fprintf(stderr, " IGNORED_WAS_NOT_PLAYING");
    goto end;
  }
  
  player->paused= 1;
  tracker(-1);
  err= AudioQueuePause(player->AQ);
  if (err) {
    fprintf(stderr, " ERROR:AudioQueuePause:[%d]", err);
  }

#else
  fprintf(stderr, "\nERROR *** Sound::pause() LINUX not implemented argghh");
  fflush(stderr);
  
  
#endif

  end:
  //fflush(stderr);
  return scope.Close(args.This());
}








// =========================
// = **** DestroyerCB **** =
// =========================

void destroyerCB (v8::Persistent<v8::Value> object, void* parameter) {
  
#if defined (__APPLE__)
  
  OSStatus err;
  playerStruct* player= (playerStruct*) parameter;
  
  //fprintf(stderr, "\n*** destroyerCB() [%ld]", player->id);
  
  if (player->playing || player->hasCallback || player->callbackIsPending) {
    // Habrá que esperar a que termine antes de cargárselo...
    
    //fprintf(stderr, "\n[%ld] NOT_DESTROYED_WAS_PLAYING_OR_HAS_PENDING_CB", player->id);
    
    object.MakeWeak(player, destroyerCB);
    wasPlayingCtr++;
    goto end;
  }
  
  
  player->destroying= 1;
  err= AudioQueueDispose(player->AQ, true);
  if (err) {
    
    //fprintf(stderr, "\n[%ld] DESTROYING: AudioQueueDispose ERROR:[%d]", player->id, err);
    
    player->destroying= 0;
    object.MakeWeak((void*) player, destroyerCB);
    goto end;
  }
  else {
    
    //fprintf(stderr, "\n[%ld] DESTROYED", player->id);
    
    v8::V8::AdjustAmountOfExternalAllocatedMemory(-(2*player->bufferLength + sizeof(playerStruct)));
    free(player);
    object.Dispose();
    destroyedCtr++;
  }
  
  
#else
  fprintf(stderr, "\nERROR *** destroyerCB() LINUX not implemented argghh");
  fflush(stderr);
  
  
#endif

  end:
  //fflush(stderr);
  return;
}





// ====================
// = **** Create **** =
// ====================

v8::Handle<v8::Value> Create (const v8::Arguments &args) {
  
  //fprintf(stderr, "\nOK *** Sound::Create() BEGIN");
  //fflush(stderr);
  
  v8::HandleScope scope;
  
  if (args.Length() != 1) {
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer): bad number of arguments")));
  }
  
  if (!node::Buffer::HasInstance(args[0])) {
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer): The argument must be a Buffer() instance")));
  }
  
  //v8::Persistent<Object> buffer= v8::Persistent<Object>::New(args[0]->ToObject());
  v8::Local<v8::Object> buffer= args[0]->ToObject();
  char* bufferData= node::Buffer::Data(buffer);
  size_t bufferLength= node::Buffer::Length(buffer);
  
  if (!bufferLength) {
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer): buffer has length == 0")));
  }
  
  if (bufferLength < 8) {
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer): buffer.length must be >= 8")));
  }
  
  if (bufferLength % 4) {
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer): buffer.length must a multiple of 4")));
  }
  
#if defined (__APPLE__)

  OSStatus err;
  playerStruct* player= newPlayer();
  
  player->AQBuffer1Length= bufferLength/2;
  (player->AQBuffer1Length % 4) && (player->AQBuffer1Length-= (player->AQBuffer1Length % 4));
  player->AQBuffer2Length= bufferLength- player->AQBuffer1Length;
  
  //fprintf(stderr, "\nOK *** AQBufferSize -> [%ld, %ld]", player->AQBuffer1Length, player->AQBuffer2Length);
  //fflush(stderr);
  
  
  
  err= AudioQueueNewOutput(
    player->format,         // const AudioStreamBasicDescription   *inFormat
    AQBufferCallback,        // AudioQueueOutputCallback            inCallbackProc
    player,                  // void                                *inUserData
    NULL,                    // CFRunLoopRef                        inCallbackRunLoop
    kCFRunLoopDefaultMode,   // CFStringRef                         inCallbackRunLoopMode
    0,                       // UInt32                              inFlags
    &player->AQ              // AudioQueueRef                       *outAQ
  );
  
  if (err) {
    free(player);
    v8::V8::AdjustAmountOfExternalAllocatedMemory((int) -sizeof(playerStruct));
    fprintf(stderr, "\nERROR *** Sound::create AudioQueueNewOutput:[%d]\n", err);
    fflush(stderr);
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer) AudioQueueNewOutput error")));
  }
  
  /*
    TODO 
    - Hay que hacer un método rewind() ?.
  */
  
  err= AudioQueueAllocateBuffer (
    player->AQ,               // AudioQueueRef inAQ
    player->AQBuffer1Length,  // UInt32 inBufferByteSize
    &player->AQBuffer1        // AudioQueueBufferRef *outBuffer
  );
  
  if (err) {
    AudioQueueDispose (player->AQ, true);
    free(player);
    v8::V8::AdjustAmountOfExternalAllocatedMemory((int) -sizeof(playerStruct));
    fprintf(stderr, "\nERROR *** Sound::create AudioQueueAllocateBuffer:[%d]\n", err);
    fflush(stderr);
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer) AudioQueueAllocateBuffer error")));
  }
  
  err= AudioQueueAllocateBuffer (
    player->AQ,                // AudioQueueRef inAQ
    player->AQBuffer2Length,   // UInt32 inBufferByteSize
    &player->AQBuffer2         // AudioQueueBufferRef *outBuffer
  );
  
  if (err) {
    AudioQueueDispose (player->AQ, true);
    free(player);
    v8::V8::AdjustAmountOfExternalAllocatedMemory((int) -sizeof(playerStruct));
    fprintf(stderr, "\nERROR *** Sound::create AudioQueueAllocateBuffer:[%d]\n", err);
    fflush(stderr);
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::create(buffer) AudioQueueAllocateBuffer error")));
  }
  
  v8::V8::AdjustAmountOfExternalAllocatedMemory(bufferLength);
  memcpy(player->AQBuffer1->mAudioData, bufferData, player->AQBuffer1Length);
  memcpy(player->AQBuffer2->mAudioData, (bufferData+ player->AQBuffer1Length), player->AQBuffer2Length);
  
  //fprintf(stderr, "\nOK *** Sound::Create() END");
  
#else
  fprintf(stderr, "\nERROR *** Sound::create() LINUX not implemented argghh.");
  fflush(stderr);

  void* player= newPlayer();

#endif
  
  v8::Persistent<v8::Object> JSObject= v8::Persistent<v8::Object>::New(v8::Object::New());
  JSObject.MakeWeak(player, destroyerCB);
  player->JSObject= JSObject;
  
  
  JSObject->Set(id_symbol, v8::Integer::New(player->id));
  JSObject->Set(play_symbol, play_function);
  JSObject->Set(pause_symbol, pause_function);
  JSObject->Set(volume_symbol, volume_function);
  JSObject->Set(loop_symbol, loop_function);
  JSObject->Set(data_symbol, buffer);
  JSObject->SetHiddenValue(hiddenPlayerPtr_symbol, v8::External::Wrap(player));
  
  
  if ((createdCtr % 500) == 0) {
    fprintf(stderr, "\nGC *** Sound::create [Created:%ld, Destroyed:%ld, WerePlaying:%ld]\n", createdCtr, destroyedCtr, wasPlayingCtr);
    fflush(stderr);
  }
  
  //fprintf(stderr, "\nOK *** Create\n");
  //fflush(stderr);
  
  return scope.Close(JSObject);
}





// =================
// = renderSound() =
// =================

void renderSound (renderJob* job) {
  CFDataRef strChars;
  strChars= CFDataCreate(NULL, (UInt8*) job->str, job->strLen);
  
  CFStringRef pathStr;
  pathStr= CFStringCreateFromExternalRepresentation(NULL, strChars, kCFStringEncodingUTF8);
  
  CFURLRef pathURL;
  pathURL= CFURLCreateWithFileSystemPath(NULL, pathStr, kCFURLPOSIXPathStyle, false);
  
  OSStatus err;
  ExtAudioFileRef inputAudioFile;
  err= ExtAudioFileOpenURL(pathURL, &inputAudioFile);
  if (err) {
    fprintf(stderr, "\nERROR ExtAudioFileOpenURL [%d]", err);
    return;
  }
  
  UInt32 size;
  Boolean writable;
  err= ExtAudioFileGetPropertyInfo(inputAudioFile, kExtAudioFileProperty_FileDataFormat, &size, &writable);
  if (err) {
    fprintf(stderr, "\nERROR ExtAudioFileGetPropertyInfo [%d]", err);
    return;
  }
  
  AudioStreamBasicDescription* inputFormat;
  inputFormat= (AudioStreamBasicDescription*) malloc(size);
  err= ExtAudioFileGetProperty(inputAudioFile, kExtAudioFileProperty_FileDataFormat, &size, inputFormat);
  if (err) {
    fprintf(stderr, "\nERROR ExtAudioFileGetProperty [%d]", err);
    return;
  }
  
  fprintf(stderr, "\nmSampleRate: %lf", inputFormat->mSampleRate);
  fprintf(stderr, "\nmFormatID: %d", inputFormat->mFormatID);
  fprintf(stderr, "\nmFormatFlags: %d", inputFormat->mFormatFlags);
  fprintf(stderr, "\nmBytesPerPacket: %d", inputFormat->mBytesPerPacket);
  fprintf(stderr, "\nmFramesPerPacket: %d", inputFormat->mFramesPerPacket);
  fprintf(stderr, "\nmBytesPerFrame: %d", inputFormat->mBytesPerFrame);
  fprintf(stderr, "\nmChannelsPerFrame: %d", inputFormat->mChannelsPerFrame);
  fprintf(stderr, "\nmBitsPerChannel: %d", inputFormat->mBitsPerChannel);
  fprintf(stderr, "\nmReserved: %d", inputFormat->mReserved);
  
  
  err= ExtAudioFileSetProperty(inputAudioFile, kExtAudioFileProperty_ClientDataFormat, size, &gFormato);
  if (err) {
    fprintf(stderr, "\nERROR ExtAudioFileSetProperty [%d]", err);
    return;
  }
  
  #define kBufferSize 4*1024*1024
  
  UInt32 frames;
  bufferStruct* buffer;
  queueStruct* bufferQItem;
  job->qHead= bufferQItem= NULL;
  job->bytesRead= 0;
  AudioBufferList bufferList;
  
  do {
    buffer= newBuffer(kBufferSize);
    bufferQItem= newQueueItem(buffer, job->qHead);
    if (job->qHead == NULL) job->qHead= bufferQItem;
    
    frames= buffer->size/4;
    bufferList.mNumberBuffers= 1;
    bufferList.mBuffers[0].mNumberChannels= 2;
    bufferList.mBuffers[0].mDataByteSize= buffer->size;
    bufferList.mBuffers[0].mData= buffer->buffer;
  
    err= ExtAudioFileRead (inputAudioFile, &frames, &bufferList);
    if (err) {
      fprintf(stderr, "\nERROR ExtAudioFileRead [%d]", err);
      return;
    }
  
    job->bytesRead+= (buffer->used= frames*4);
    
    fprintf(stderr, "\nSe han convertido %d frames", frames);
    
  } while (frames);
  
  
  end:
  free(inputFormat);
  err= ExtAudioFileDispose(inputAudioFile);
  if (err) {
    fprintf(stderr, "\nERROR ExtAudioFileDispose [%d]", err);
    return;
  }
  CFRelease(pathURL);
  CFRelease(pathStr);
  CFRelease(strChars);
}





// =============================
// = **** BufferifySync() **** =
// =============================

v8::Handle<v8::Value> BufferifySync (const v8::Arguments &args) {
  
  v8::HandleScope scope;
  
  ssize_t bytes;
  ssize_t offset;
  char* nodeBufferData;
  bufferStruct* buffer;
  node::Buffer* nodeBuffer;
  queueStruct* bufferQItem;
  v8::Local<v8::String> str;
  renderJob job;
  
  if ((args.Length() != 1) && !args[0]->IsString()) {
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::bufferifySync(): bad arguments")));
  }
  
  str= args[0]->ToString();
  job.str= *v8::String::Utf8Value(str);
  job.strLen= str->Utf8Length();
  job.bytesRead= 0;
  job.qHead= NULL;
  
  renderSound(&job);
  
  if (job.qHead != NULL) {
    
    bytes= job.bytesRead;
    if (bytes) {
      nodeBuffer= node::Buffer::New(bytes);
      nodeBufferData= node::Buffer::Data(nodeBuffer->handle_);
    }


    offset= 0;
    bufferQItem= job.qHead;
    while (bufferQItem) {
      buffer= (bufferStruct*) bufferQItem->item;
      if (bytes && buffer->used) {
        memcpy(nodeBufferData+ offset, buffer->buffer, buffer->used);
        offset+= buffer->used;
      }
      destroyBuffer(buffer);
      bufferQItem= destroyQueueItem(bufferQItem);
    }

    if (bytes) return scope.Close(nodeBuffer->handle_);
  }
  
  end:
  //fprintf(stderr, "\nOK *** BufferifySync\n");
  fprintf(stderr, "\n");
  fflush(stderr);

  return v8::Undefined();
}









// =========================
// = **** Bufferify() **** =
// =========================

v8::Handle<v8::Value> Bufferify (const v8::Arguments &args) {
  
  v8::HandleScope scope;
  
  ssize_t bytes;
  ssize_t offset;
  char* nodeBufferData;
  bufferStruct* buffer;
  node::Buffer* nodeBuffer;
  queueStruct* bufferQItem;
  v8::Local<v8::String> str;
  renderJob* job;
  v8::Local<v8::Value> argv[2];
  
  if ((args.Length() != 2) || (!(args[0]->IsString() && args[1]->IsFunction()))) {
    return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Sound::bufferify(): bad arguments")));
  }
  
  str= args[0]->ToString();
  job= (renderJob*) malloc(sizeof(renderJob));
  job->strLen= str->Utf8Length();
  job->str= (char*) malloc(job->strLen);
  strncpy(job->str, *v8::String::Utf8Value(str), job->strLen);
  job->bytesRead= 0;
  job->qHead= NULL;
  job->JSCallback= v8::Persistent<v8::Object>::New(args[1]->ToObject());
  
  renderSound(job);
  free(job->str);
  
  if (job->qHead != NULL) {
    
    bytes= job->bytesRead;
    if (bytes) {
      nodeBuffer= node::Buffer::New(bytes);
      nodeBufferData= node::Buffer::Data(nodeBuffer->handle_);
    }


    offset= 0;
    bufferQItem= job->qHead;
    while (bufferQItem) {
      buffer= (bufferStruct*) bufferQItem->item;
      if (bytes && buffer->used) {
        memcpy(nodeBufferData+ offset, buffer->buffer, buffer->used);
        offset+= buffer->used;
      }
      destroyBuffer(buffer);
      bufferQItem= destroyQueueItem(bufferQItem);
    }
    
    argv[0]= v8::Integer::New(0);
    argv[1]= v8::Local<v8::Object>::Cast(nodeBuffer->handle_->ToObject());
    v8::Persistent<v8::Function>::Cast(job->JSCallback)->Call(v8::Context::GetCurrent()->Global(), 2, argv);
    
    //if (bytes) return scope.Close(nodeBuffer->handle_);
  }
  
  end:
  //fprintf(stderr, "\nOK *** BufferifySync\n");
  fprintf(stderr, "\n");
  fflush(stderr);

  return v8::Undefined();
  
}











// ===================================
// = **** Callback into node.js **** =
// ===================================

// this is the async libev event callback that runs in node.js main thread
static void Callback (EV_P_ ev_async *watcher, int revents) {
  
  //fprintf(stderr, "*** JSCallback");
  //fflush(stderr);
  
  v8::HandleScope scope;

  assert(watcher == &eio_sound_async_notifier);
  assert(revents == EV_ASYNC);
  
  queueStruct* qitem;
  // Grab the queue.
  pthread_mutex_lock(&callbacksQueue_mutex);
  qitem= callbacksQueue;
  callbacksQueue= NULL;
  pthread_mutex_unlock(&callbacksQueue_mutex);
  
  /*
    TODO 
    - ver qué hay que hacer exactamente cuando cb() throws.
  */
  
  //TryCatch try_catch;
  
  while (qitem != NULL) {
    playerStruct* player;
    player= (playerStruct*) qitem->item;
    
    if (player->callbackIsPending) {
      v8::Persistent<v8::Object> cb= player->pendingJSCallback;
      v8::Persistent<v8::Function>::Cast(cb)->Call(player->JSObject, 0, NULL);
      player->callbackIsPending= 0;
      cb.Dispose();
      //if (try_catch.HasCaught()) FatalException(try_catch);
    }
    
    qitem= destroyQueueItem(qitem);
  }
}




// ============
// = Stream() =
// ============

v8::Handle<v8::Value> Stream (const v8::Arguments &args) {
  
  v8::HandleScope scope;
  
  return v8::Undefined();
}



// =================================
// = **** Initialization code **** =
// =================================

// Esto se llama una sola vez, al hacer require('sound');
extern "C" {
  void init (v8::Handle<v8::Object> target) {
    
    v8::HandleScope scope;

    volume_symbol= v8::Persistent<v8::String>::New(v8::String::New("volume"));
    play_symbol= v8::Persistent<v8::String>::New(v8::String::New("play"));
    loop_symbol= v8::Persistent<v8::String>::New(v8::String::New("loop"));
    pause_symbol= v8::Persistent<v8::String>::New(v8::String::New("pause"));
    
    id_symbol= v8::Persistent<v8::String>::New(v8::String::New("id"));
    data_symbol= v8::Persistent<v8::String>::New(v8::String::New("data"));
    hiddenPlayerPtr_symbol= v8::Persistent<v8::String>::New(v8::String::New("_hiddenPlayerPtr"));
    
    volume_function= v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(Volume)->GetFunction());
    loop_function= v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(Loop)->GetFunction());
    play_function= v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(Play)->GetFunction());
    pause_function= v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(Pause)->GetFunction());
    
    target->Set(v8::String::New("create"), v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(Create)->GetFunction()));
    target->Set(v8::String::New("bufferify"), v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(Bufferify)->GetFunction()));
    target->Set(v8::String::New("bufferifySync"), v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(BufferifySync)->GetFunction()));
    target->Set(v8::String::New("stream"), v8::Persistent<v8::Function>::New(v8::FunctionTemplate::New(Stream)->GetFunction()));
    
    // Start async events for callbacks.
    ev_async_init(&eio_sound_async_notifier, Callback);
    ev_async_start(EV_DEFAULT_UC_ &eio_sound_async_notifier);
    ev_unref(EV_DEFAULT_UC);
    
#if defined (__APPLE__)
    gFormato.mSampleRate= 44100;
    gFormato.mFormatID= kAudioFormatLinearPCM;
    gFormato.mFormatFlags= kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    gFormato.mBytesPerPacket= 4;
    gFormato.mFramesPerPacket= 1;
    gFormato.mBytesPerFrame= 4;
    gFormato.mChannelsPerFrame= 2;
    gFormato.mBitsPerChannel= 16;
    
    /*
      This is a dummy player to init the sound machinery and to keep it up and running when no other sounds are playing.
      It does not need any buffers, it just plays silence. It's paused when not needed (when other sounds are playing)
      and restarted when no other sounds are playing (tracker() keeps track of that).
    */
    
    OSStatus err;
    fondoSnd= newPlayer();
    err= AudioQueueNewOutput(
      fondoSnd->format,         // const AudioStreamBasicDescription   *inFormat
      AQBufferCallback,          // AudioQueueOutputCallback            inCallbackProc
      fondoSnd,                  // void                                *inUserData
      NULL,                      // CFRunLoopRef                        inCallbackRunLoop
      kCFRunLoopDefaultMode,     // CFStringRef                         inCallbackRunLoopMode
      0,                         // UInt32                              inFlags
      &fondoSnd->AQ              // AudioQueueRef                       *outAQ
    );
    
    err= AudioQueueStart(fondoSnd->AQ, NULL);
    if (err) {
      fprintf(stderr, "\nERROR *** Sound::Init AudioQueueStart:[%d]\n", err);
    }
    
#endif

    fprintf(stderr, "\nOK *** Sound::init");
    
#if defined (__APPLE__)
    fprintf(stderr, " [MAC] ");
#else
    fprintf(stderr, " [LINUX] ");
#endif

    fprintf(stderr, " © 2011 Jorge@jorgechamorro.com\n");
    fflush(stderr);
  }

  NODE_MODULE(sound, init);
}
