//
// OBS API for Electron applications
//
// The API supports:
// - Startup OBS
// - Initialize the audio and video contexts
// - Load OBS modules
// - Create audio and video encoders
// - Create Twitch service with server URL + streamkey
// - Create an RTMP output
// - Set the encoders and the service to the output
// - Start / stop the output
// - Shutdown OBS
//

#include <napi.h>
#include <obs.h>
#include <string>

#define DEFAULT_VIDEO_ADAPTER 0
#define DEFAULT_MODULE ("libobs-opengl")
#define DEFAULT_VIDEO_FORMAT VIDEO_FORMAT_I420
#define DEFAULT_VIDEO_FPS_NUM 30000
#define DEFAULT_VIDEO_FPS_DEN 1000
#define DEFAULT_VIDEO_WIDTH 640
#define DEFAULT_VIDEO_HEIGHT 360

#define DEFAULT_AUDIO_SAMPLES 44100
#define DEFAULT_AUDIO_CHANNELS SPEAKERS_STEREO

#define NOT_INITIALIZED_STRING ("Error: OBS API not initialized!")

obs_video_info create_ovi(
    size_t adapter = DEFAULT_VIDEO_ADAPTER,
    const char* graphics_module = DEFAULT_MODULE,
    const enum video_format output_format = DEFAULT_VIDEO_FORMAT,
    size_t fps_num = DEFAULT_VIDEO_FPS_NUM,
    size_t fps_den = DEFAULT_VIDEO_FPS_DEN,
    size_t base_width = DEFAULT_VIDEO_WIDTH,
    size_t base_height = DEFAULT_VIDEO_HEIGHT,
    size_t output_width = DEFAULT_VIDEO_WIDTH,
    size_t output_height = DEFAULT_VIDEO_HEIGHT
    ) 
{
    struct obs_video_info ovi;

    ovi.adapter = adapter;
    ovi.graphics_module = graphics_module;
    ovi.output_format = output_format;
    ovi.fps_num = fps_num;
    ovi.fps_den = fps_den;
    ovi.base_width = base_width;
    ovi.base_height = base_height;
    ovi.output_width = output_width;
    ovi.output_height = output_height;

    return ovi;
}

obs_audio_info create_oai(
  size_t samples_per_sec = DEFAULT_AUDIO_SAMPLES,
  const enum speaker_layout speakers = DEFAULT_AUDIO_CHANNELS) 
{
    struct obs_audio_info oai;

    oai.samples_per_sec = samples_per_sec;
    oai.speakers = speakers;

    return oai;
}

// Enumerates encoders
bool enumCodecs(void* c, obs_encoder_t* encoder) {
  if(c && encoder) { 
    std::string* codecs = (std::string*)c;
    *codecs += obs_encoder_get_name(encoder);
    return true;
  }

  return false;
}

Napi::String obsGetCodecs(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if(obs_initialized()) {
    std::string codecs = "";
    obs_enum_encoders(enumCodecs, &codecs);
    
    return Napi::String::New(env, codecs);
  }
  else
    return Napi::String::New(env, NOT_INITIALIZED_STRING);
} 

// Enumerates outputs.
// 
bool enumOutputs(void* c, obs_output_t* encoder) {
  if(c && encoder) { 
    std::string* codecs = (std::string*)c;
    *codecs += obs_output_get_name(encoder);
    return true;
  }

  return false;
}

Napi::String obsGetOutputs(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if(obs_initialized()) {
    std::string outputs = "";
    obs_enum_outputs(enumOutputs, &outputs);
    
    return Napi::String::New(env, outputs);
  }
  else
    return Napi::String::New(env, NOT_INITIALIZED_STRING);
} 

// Asynchronously initializes the OBS core context. 
class AsyncInitializeWorker : public Napi::AsyncWorker {
public:
  static Napi::Value Create(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    //   :param  locale:             The locale to use for modules
    //                               (E.G. "en-US")
    //   :param  module_config_path: Path to module config storage directory
    //                               (or *NULL* if none)
    //   :param  store:              The profiler name store for OBS to use or NULL
    obs_startup("en-US", nullptr, nullptr);
    if (!obs_initialized()) {
      Napi::TypeError::New(env, NOT_INITIALIZED_STRING)
          .ThrowAsJavaScriptException();
      return env.Null();
    }
    
    obs_load_all_modules();

    AsyncInitializeWorker* worker = new AsyncInitializeWorker(info.Env());

    worker->Queue();
    return worker->deferredPromise.Promise();
  }

protected:
  void Execute() override {
    result = "v" + std::string(obs_get_version_string());
  }

  virtual void OnOK() override {
      deferredPromise.Resolve(Napi::String::New(Env(), result));
  }

  virtual void OnError(const Napi::Error& e) override {
      deferredPromise.Reject(e.Value());
  }

private:
  AsyncInitializeWorker(napi_env env) :
    Napi::AsyncWorker(env),
    result(),
    deferredPromise(Napi::Promise::Deferred::New(env)) { }

  std::string result;

  Napi::Promise::Deferred deferredPromise;
};

// Releases all data associated with OBS and terminates the OBS context
//
Napi::String obsShutdown(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if(obs_initialized()) {
    obs_shutdown();
    return Napi::String::New(env, "Success shutting down OBS");
  }
  else
    return Napi::String::New(env, NOT_INITIALIZED_STRING);
}

// Asynchronously sets base video output base resolution/fps/format
// Note: This data cannot be changed if an output is currently active.
// Note: The graphics module cannot be changed without fully destroying the OBS context.
//
class AsyncResetVideoWorker : public Napi::AsyncWorker {
public:
  static Napi::Value Create(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!obs_initialized()) {
      Napi::TypeError::New(env, NOT_INITIALIZED_STRING)
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    if (info.Length() != 1 || !info[0].IsString()) {
      Napi::TypeError::New(env, "Expected a single string argument")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    std::string input = info[0].As<Napi::String>();

    AsyncResetVideoWorker* worker = new AsyncResetVideoWorker(info.Env(), input);

    worker->Queue();
    return worker->deferredPromise.Promise();
  }

protected:
  void Execute() override {
    size_t width = DEFAULT_VIDEO_WIDTH;
    size_t height = DEFAULT_VIDEO_HEIGHT;
    size_t xpos = input.find('x');
    if(xpos != std::string::npos) {
      std::string w = input.substr(0, xpos);
      input.erase(0, xpos + 1);
      std::string h = input;
      if(w.length()) {
        width = atoi(w.c_str());
      };
      if(h.length()) {
        height = atoi(h.c_str());
      };
    }

    ovi = create_ovi(DEFAULT_VIDEO_ADAPTER, DEFAULT_MODULE, DEFAULT_VIDEO_FORMAT, 
      DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN, width, height);

    obs_reset_video(&ovi);
  }

  virtual void OnOK() override {
      deferredPromise.Resolve(Napi::String::New(Env(), 
        std::to_string(ovi.base_width) + 'x' + std::to_string(ovi.base_height)));
  }

  virtual void OnError(const Napi::Error& e) override {
      deferredPromise.Reject(e.Value());
  }

private:
  struct obs_video_info ovi;
  AsyncResetVideoWorker(napi_env env, std::string& hint) :
    Napi::AsyncWorker(env),
    input(hint),
    deferredPromise(Napi::Promise::Deferred::New(env)) { }

  std::string input;
  Napi::Promise::Deferred deferredPromise;
};

// Asynchronously sets base audio output format/channels/samples/etc.
// Note: Cannot reset base audio if an output is currently active.
//
class AsyncResetAudioWorker : public Napi::AsyncWorker {
public:
  static Napi::Value Create(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!obs_initialized()) {
      Napi::TypeError::New(env, NOT_INITIALIZED_STRING)
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    if (info.Length() != 1 || !info[0].IsString()) {
      Napi::TypeError::New(env, "Expected a single string argument")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    std::string input = info[0].As<Napi::String>();

    AsyncResetAudioWorker* worker = new AsyncResetAudioWorker(info.Env(), input);

    worker->Queue();
    return worker->deferredPromise.Promise();
  }

protected:
  void Execute() override {
    size_t mpos = input.find("mono");
    bool stereo = true;
    if(mpos != std::string::npos) {
      stereo = false;
    }

    oai = create_oai(DEFAULT_AUDIO_SAMPLES, stereo? SPEAKERS_STEREO : SPEAKERS_MONO);

    obs_reset_audio(&oai);
  }

  virtual void OnOK() override {
      deferredPromise.Resolve(Napi::String::New(Env(), 
        oai.speakers == SPEAKERS_STEREO ? "stereo" : "mono"));
  }

  virtual void OnError(const Napi::Error& e) override {
      deferredPromise.Reject(e.Value());
  }

private:
  struct obs_audio_info oai;
  AsyncResetAudioWorker(napi_env env, std::string& hint) :
    Napi::AsyncWorker(env),
    input(hint),
    deferredPromise(Napi::Promise::Deferred::New(env)) { }

  std::string input;
  Napi::Promise::Deferred deferredPromise;
};

// Asynchronously start output
//
class AsyncStartOutputWorker : public Napi::AsyncWorker {
public:
  static Napi::Value Create(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!obs_initialized()) {
      Napi::TypeError::New(env, NOT_INITIALIZED_STRING)
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    if (info.Length() != 1 || !info[0].IsString()) {
      Napi::TypeError::New(env, "Expected a single string argument")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    std::string input = info[0].As<Napi::String>();

    AsyncStartOutputWorker* worker = new AsyncStartOutputWorker(info.Env(), input);

    worker->Queue();
    return worker->deferredPromise.Promise();
  }

protected:
  void Execute() override {
    video_encoder = obs_video_encoder_create("com.apple.videotoolbox.videoencoder.h264.gva", 
        "",
        NULL, 
        NULL);

    audio_encoder = obs_audio_encoder_create("adv_stream_aac", 
        "",
        NULL, 
        0, 
        NULL);

    obs_encoder_set_video(video_encoder, obs_get_video());
    obs_encoder_set_audio(audio_encoder, obs_get_audio());
    obs_output_set_video_encoder(output, video_encoder);
    obs_output_set_audio_encoder(output, audio_encoder, audio_index);
    obs_output_set_service(output, streaming_service); /* if a stream */
    obs_output_start(output);
  }

  virtual void OnOK() override {
    cleanup();
    deferredPromise.Resolve(Napi::String::New(Env(), "ok"));
  }

  virtual void OnError(const Napi::Error& e) override {
    cleanup();
    deferredPromise.Reject(e.Value());
  }

private:
  AsyncStartOutputWorker(napi_env env, std::string& hint) :
    Napi::AsyncWorker(env),
    input(hint),
    deferredPromise(Napi::Promise::Deferred::New(env)) { }

  obs_encoder_t* video_encoder = nullptr;
  obs_encoder_t* audio_encoder = nullptr;
  obs_output_t* output = nullptr;
  obs_service_t* streaming_service = nullptr;
  size_t audio_index = 0;

  void cleanup() {
    if(output) {
      obs_output_stop(output);
      obs_output_release(output);
      output = nullptr;
    }

    if(video_encoder) {
      obs_encoder_release(video_encoder);
      video_encoder = nullptr;
    }
    if(audio_encoder) {
      obs_encoder_release(audio_encoder);
      audio_encoder = nullptr;
    }

    if(streaming_service) {
      obs_service_release(streaming_service);
      streaming_service = nullptr;
    }
  }

  std::string input;
  Napi::Promise::Deferred deferredPromise;
};

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "initialize"),
              Napi::Function::New(env, AsyncInitializeWorker::Create));
  exports.Set(Napi::String::New(env, "shutdown"),
              Napi::Function::New(env, obsShutdown));
  exports.Set(Napi::String::New(env, "resetVideo"),
              Napi::Function::New(env, AsyncResetVideoWorker::Create));
  exports.Set(Napi::String::New(env, "resetAudio"),
              Napi::Function::New(env, AsyncResetAudioWorker::Create));
  exports.Set(Napi::String::New(env, "startOutput"),
              Napi::Function::New(env, AsyncStartOutputWorker::Create));
  exports.Set(Napi::String::New(env, "getCodecs"),
              Napi::Function::New(env, obsGetCodecs));
  exports.Set(Napi::String::New(env, "getOutputs"),
              Napi::Function::New(env, obsGetOutputs));
  return exports;
}

NODE_API_MODULE(obsapi, Init)