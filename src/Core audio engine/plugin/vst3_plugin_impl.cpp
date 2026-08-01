#include "vst3_plugin_impl.h"
#include <project_config.h>
#include "Hardware/OS abstraction/audio/audio_utils.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <thread>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
typedef bool (*BundleEntryProc)(CFBundleRef);
typedef bool (*BundleExitProc)();
#endif

namespace Layer3 {

// Minimal VST3 EventList Implementation
class EventList : public Steinberg::Vst::IEventList {
public:
    EventList() : m_refCount(0) {
        m_events.reserve(512); // Pre-allocate to avoid heap allocation in RT thread
    }
    virtual ~EventList() {}
    
    Steinberg::int32 PLUGIN_API getEventCount() override { return (Steinberg::int32)m_events.size(); }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override {
        if (index < 0 || (size_t)index >= m_events.size()) return Steinberg::kResultFalse;
        e = m_events[(size_t)index];
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        if (m_events.size() < 512) {
            m_events.push_back(e);
            return Steinberg::kResultOk;
        }
        return Steinberg::kResultFalse;
    }
    
    void clear() { m_events.clear(); }
    
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IEventList::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IEventList*>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { 
        return m_refCount.fetch_add(1, std::memory_order_relaxed) + 1; 
    }
    Steinberg::uint32 PLUGIN_API release() override { 
        uint32_t rc = m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (rc == 0) {
            delete this;
        }
        return rc;
    }

private:
    std::atomic<uint32_t> m_refCount;
    std::vector<Steinberg::Vst::Event> m_events;
};

// Minimal VST3 ParameterChanges Implementation
class ParameterValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    ParameterValueQueue() : m_id(0), m_refCount(0), m_size(0) {
        m_points.resize(64); // Pre-allocate points vector to avoid RT allocation
    }
    virtual ~ParameterValueQueue() = default;
    
    void setParameterId(Steinberg::Vst::ParamID id) { m_id = id; }
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return m_id; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return (Steinberg::int32)m_size; }
    
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset, Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || (size_t)index >= m_size) return Steinberg::kResultFalse;
        sampleOffset = m_points[(size_t)index].first;
        value = m_points[(size_t)index].second;
        return Steinberg::kResultOk;
    }
    
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value, Steinberg::int32& index) override {
        if (m_size >= m_points.size()) {
            return Steinberg::kResultFalse;
        }
        m_points[m_size] = {sampleOffset, value};
        index = (Steinberg::int32)m_size;
        m_size++;
        return Steinberg::kResultOk;
    }
    
    void clear() { m_size = 0; }
    
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IParamValueQueue::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IParamValueQueue*>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    
    Steinberg::uint32 PLUGIN_API addRef() override { 
        return m_refCount.fetch_add(1, std::memory_order_relaxed) + 1; 
    }
    Steinberg::uint32 PLUGIN_API release() override { 
        uint32_t rc = m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (rc == 0) {
            delete this;
        }
        return rc;
    }
    
private:
    Steinberg::Vst::ParamID m_id;
    std::atomic<uint32_t> m_refCount;
    std::vector<std::pair<Steinberg::int32, Steinberg::Vst::ParamValue>> m_points;
    size_t m_size = 0;
};

class ParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    ParameterChanges() : m_refCount(0), m_activeCount(0) {
        // Pre-allocate a pool of queues to avoid NRT allocations
        for (int i = 0; i < 128; ++i) {
            m_queues.push_back(std::make_unique<ParameterValueQueue>());
        }
    }
    virtual ~ParameterChanges() = default;
    
    Steinberg::int32 PLUGIN_API getParameterCount() override { 
        return (Steinberg::int32)m_activeCount; 
    }
    
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || (size_t)index >= m_activeCount) return nullptr;
        return m_queues[(size_t)index].get();
    }
    
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override {
        for (size_t i = 0; i < m_activeCount; ++i) {
            if (m_queues[i]->getParameterId() == id) {
                index = (Steinberg::int32)i;
                return m_queues[i].get();
            }
        }
        if (m_activeCount >= m_queues.size()) {
            return nullptr;
        }
        size_t idx = m_activeCount++;
        m_queues[idx]->setParameterId(id);
        m_queues[idx]->clear();
        index = (Steinberg::int32)idx;
        return m_queues[idx].get();
    }
    
    void clear() { 
        for (size_t i = 0; i < m_activeCount; ++i) {
            m_queues[i]->clear(); 
        }
        m_activeCount = 0;
    }
    
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IParameterChanges::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IParameterChanges*>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    
    Steinberg::uint32 PLUGIN_API addRef() override { 
        return m_refCount.fetch_add(1, std::memory_order_relaxed) + 1; 
    }
    Steinberg::uint32 PLUGIN_API release() override { 
        uint32_t rc = m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (rc == 0) {
            delete this;
        }
        return rc;
    }
    
private:
    std::atomic<uint32_t> m_refCount;
    std::vector<std::unique_ptr<ParameterValueQueue>> m_queues;
    size_t m_activeCount = 0;
};

using GetFactoryProc = Steinberg::IPluginFactory *(*)();

} // namespace Layer3

namespace Steinberg {
class VstMemoryStream : public IBStream {
public:
  VstMemoryStream(const uint8_t *data, uint64_t size) : m_data(data), m_size(size), m_pos(0) {}
  
  ::Steinberg::tresult PLUGIN_API read(void* buffer, ::Steinberg::int32 numBytes, ::Steinberg::int32* numBytesRead) override {
    uint64_t bytesToRead = std::min((uint64_t)numBytes, m_size - m_pos);
    if (bytesToRead > 0) {
      std::memcpy(buffer, m_data + m_pos, bytesToRead);
      m_pos += bytesToRead;
    }
    if (numBytesRead) *numBytesRead = (::Steinberg::int32)bytesToRead;
    return ::Steinberg::kResultOk;
  }
  
  ::Steinberg::tresult PLUGIN_API write(void* buffer, ::Steinberg::int32 numBytes, ::Steinberg::int32* numBytesWritten) override { 
    (void)buffer; (void)numBytes; (void)numBytesWritten;
    return ::Steinberg::kNotImplemented; 
  }

  ::Steinberg::tresult PLUGIN_API seek(::Steinberg::int64 pos, ::Steinberg::int32 mode, ::Steinberg::int64* result) override {
    switch (mode) {
      case IBStream::kIBSeekSet: m_pos = (uint64_t)pos; break;
      case IBStream::kIBSeekCur: m_pos = (uint64_t)((int64_t)m_pos + pos); break;
      case IBStream::kIBSeekEnd: m_pos = (uint64_t)((int64_t)m_size + pos); break;
    }
    if (result) *result = (::Steinberg::int64)m_pos;
    return ::Steinberg::kResultOk;
  }
  ::Steinberg::tresult PLUGIN_API tell(::Steinberg::int64* pos) override { if (pos) *pos = (::Steinberg::int64)m_pos; return ::Steinberg::kResultOk; }
  
  ::Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  ::Steinberg::uint32 PLUGIN_API release() override { return 1; }
  ::Steinberg::tresult PLUGIN_API queryInterface(const ::Steinberg::TUID _iid, void** obj) override {
    if (::Steinberg::FUnknownPrivate::iidEqual(_iid, ::Steinberg::IBStream::iid)) {
      *obj = static_cast<::Steinberg::IBStream*>(this);
      addRef();
      return ::Steinberg::kResultOk;
    }
    if (::Steinberg::FUnknownPrivate::iidEqual(_iid, ::Steinberg::FUnknown::iid)) {
      *obj = static_cast<::Steinberg::FUnknown*>(this);
      addRef();
      return ::Steinberg::kResultOk;
    }
    *obj = nullptr;
    return ::Steinberg::kNoInterface;
  }

private:
  const uint8_t *m_data;
  uint64_t m_size;
  uint64_t m_pos;
};
} // namespace Steinberg

namespace Layer3 {
VST3PluginImpl::VST3PluginImpl(const char *path) {
  m_hostContext = std::make_unique<HostContext>(this);
  m_inputEvents = new EventList();
  m_outputEvents = new EventList();
  m_inputParams = new ParameterChanges();
  m_outputParams = new ParameterChanges();
  
  if (!load(path)) {
    std::cerr << "Failed to load VST3 plugin: " << path << std::endl;
  }
}

VST3PluginImpl::~VST3PluginImpl() { unload(); }

bool VST3PluginImpl::load(const char *path) {
  std::string targetBinaryPath = path ? path : "";
#if defined(__APPLE__)
  CFBundleRef bundleRef = nullptr;
  std::string binaryPathStr(path ? path : "");
  size_t vst3Pos = binaryPathStr.find(".vst3");
  if (vst3Pos != std::string::npos) {
    std::string bundlePath = binaryPathStr.substr(0, vst3Pos + 5);
    CFStringRef cfBundlePath = CFStringCreateWithCString(kCFAllocatorDefault, bundlePath.c_str(), kCFStringEncodingUTF8);
    if (cfBundlePath) {
      CFURLRef bundleURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfBundlePath, kCFURLPOSIXPathStyle, true);
      CFRelease(cfBundlePath);
      if (bundleURL) {
        bundleRef = CFBundleCreate(kCFAllocatorDefault, bundleURL);
        if (bundleRef) {
          CFURLRef execURL = CFBundleCopyExecutableURL(bundleRef);
          if (execURL) {
            char execBuf[PATH_MAX] = {0};
            if (CFURLGetFileSystemRepresentation(execURL, true, reinterpret_cast<UInt8*>(execBuf), sizeof(execBuf))) {
              targetBinaryPath = execBuf;
            }
            CFRelease(execURL);
          }
        }
        CFRelease(bundleURL);
      }
    }
    if (targetBinaryPath == path || targetBinaryPath.empty()) {
      size_t lastSlash = bundlePath.find_last_of("/\\");
      std::string binaryName = (lastSlash != std::string::npos) ? bundlePath.substr(lastSlash + 1) : bundlePath;
      size_t dotPos = binaryName.find_last_of('.');
      if (dotPos != std::string::npos) {
        binaryName = binaryName.substr(0, dotPos);
      }
      targetBinaryPath = bundlePath + "/Contents/MacOS/" + binaryName;
    }
  }
#endif

  m_module = dlopen(targetBinaryPath.c_str(), RTLD_NOW);
  if (!m_module) {
#if defined(__APPLE__)
    if (bundleRef) {
      CFRelease(bundleRef);
    }
#endif
    return false;
  }

#if defined(__APPLE__)
  if (bundleRef) {
    auto bundleEntry = (BundleEntryProc)dlsym(m_module, "bundleEntry");
    if (bundleEntry) {
      m_bundleRef = bundleRef;
      CFRetain(bundleRef);
      if (!bundleEntry(bundleRef)) {
        std::cerr << "bundleEntry call failed for VST3 plugin" << std::endl;
      }
    }
    CFRelease(bundleRef);
  }
#endif
 
  // 1. Call InitDll if exported
  auto initDll = (InitDllProc)dlsym(m_module, "InitDll");
  if (initDll) {
      if (!initDll()) {
          unload();
          return false;
      }
  }

  auto getFactory = (GetFactoryProc)dlsym(m_module, "GetPluginFactory");
  if (!getFactory) {
      auto exitDll = (ExitDllProc)dlsym(m_module, "ExitDll");
      if (exitDll) exitDll();
      dlclose(m_module);
      m_module = nullptr;
      return false;
  }
  Steinberg::IPluginFactory *factory = getFactory();
  if (!factory) {
      auto exitDll = (ExitDllProc)dlsym(m_module, "ExitDll");
      if (exitDll) exitDll();
      dlclose(m_module);
      m_module = nullptr;
      return false;
  }

  Steinberg::PClassInfo info;
  if (factory->getClassInfo(0, &info) != Steinberg::kResultOk) {
      auto exitDll = (ExitDllProc)dlsym(m_module, "ExitDll");
      if (exitDll) exitDll();
      dlclose(m_module);
      m_module = nullptr;
      return false;
  }

  Steinberg::IPluginFactory2* factory2 = nullptr;
  if (factory->queryInterface(Steinberg::IPluginFactory2_iid, reinterpret_cast<void**>(&factory2)) == Steinberg::kResultOk && factory2) {
      Steinberg::PClassInfo2 info2;
      if (factory2->getClassInfo2(0, &info2) == Steinberg::kResultOk && info2.name[0] != '\0') {
          std::strncpy(m_name, info2.name, sizeof(m_name) - 1);
      } else {
          std::strncpy(m_name, info.name, sizeof(m_name) - 1);
      }
      factory2->release();
  } else {
      std::strncpy(m_name, info.name, sizeof(m_name) - 1);
  }
  m_name[sizeof(m_name) - 1] = '\0';

  if (m_name[0] == '\0') {
      std::string pathStr(path ? path : "");
      size_t lastSlash = pathStr.find_last_of("/\\");
      std::string filename = (lastSlash != std::string::npos) ? pathStr.substr(lastSlash + 1) : pathStr;
      size_t dotPos = filename.find_last_of('.');
      if (dotPos != std::string::npos) {
          filename = filename.substr(0, dotPos);
      }
      std::strncpy(m_name, filename.c_str(), sizeof(m_name) - 1);
      m_name[sizeof(m_name) - 1] = '\0';
  }

  Steinberg::FUnknown *obj = nullptr;
  if (factory->createInstance(info.cid, Steinberg::Vst::IComponent::iid, (void **)&obj) != Steinberg::kResultOk) {
      auto exitDll = (ExitDllProc)dlsym(m_module, "ExitDll");
      if (exitDll) exitDll();
      dlclose(m_module);
      m_module = nullptr;
      return false;
  }
  m_component = Steinberg::IPtr<Steinberg::Vst::IComponent>(reinterpret_cast<Steinberg::Vst::IComponent *>(obj), false);

  if (m_component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void **)&m_processor) != Steinberg::kResultOk) {
      m_component = nullptr;
      auto exitDll = (ExitDllProc)dlsym(m_module, "ExitDll");
      if (exitDll) exitDll();
      dlclose(m_module);
      m_module = nullptr;
      return false;
  }

  auto hostApp = static_cast<Steinberg::Vst::IHostApplication*>(m_hostContext.get());

  if (m_component->initialize(hostApp) != Steinberg::kResultOk) {
      m_component = nullptr;
      m_processor = nullptr;
      m_controller = nullptr;
      auto exitDll = (ExitDllProc)dlsym(m_module, "ExitDll");
      if (exitDll) exitDll();
      dlclose(m_module);
      m_module = nullptr;
      return false;
  }

  // Check if component itself implements IEditController (Single Component architecture)
  Steinberg::Vst::IEditController* singleCtrl = nullptr;
  if (m_component->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&singleCtrl) == Steinberg::kResultOk && singleCtrl) {
    m_controller = Steinberg::IPtr<Steinberg::Vst::IEditController>(singleCtrl, false);
  } else {
    Steinberg::TUID controllerCID;
    if (m_component->getControllerClassId(controllerCID) == Steinberg::kResultOk) {
      Steinberg::FUnknown *ctrlObj = nullptr;
      if (factory->createInstance(controllerCID, Steinberg::Vst::IEditController::iid, (void**)&ctrlObj) == Steinberg::kResultOk && ctrlObj) {
        m_controller = Steinberg::IPtr<Steinberg::Vst::IEditController>(reinterpret_cast<Steinberg::Vst::IEditController*>(ctrlObj), false);
        m_controller->initialize(hostApp);
      }
    }
  }

  if (m_controller) {
    m_controller->setComponentHandler(static_cast<Steinberg::Vst::IComponentHandler*>(m_hostContext.get()));
    m_controller->queryInterface(Steinberg::Vst::IMidiMapping::iid, (void**)&m_midiMapping);
  }

  // Connect Component <-> Controller if distinct objects
  bool distinctObjects = false;
  if (m_controller && m_component) {
      Steinberg::FUnknown* unkComp = nullptr;
      Steinberg::FUnknown* unkCtrl = nullptr;
      if (m_component->queryInterface(Steinberg::FUnknown::iid, (void**)&unkComp) == Steinberg::kResultOk &&
          m_controller->queryInterface(Steinberg::FUnknown::iid, (void**)&unkCtrl) == Steinberg::kResultOk) {
          distinctObjects = (unkComp != unkCtrl);
      }
      if (unkComp) unkComp->release();
      if (unkCtrl) unkCtrl->release();
  }

  if (distinctObjects) {
      Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> cpComponent(m_component);
      Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> cpController(m_controller);
      if (cpComponent && cpController) {
          cpComponent->connect(cpController);
          cpController->connect(cpComponent);
      }
  }

  // Synchronize Component State to EditController
  if (m_controller && m_component) {
      class SyncStateStream : public Steinberg::IBStream {
      public:
          SyncStateStream() : pos(0) {}
          Steinberg::tresult PLUGIN_API read(void* b, Steinberg::int32 n, Steinberg::int32* readBytes) override {
              uint64_t bytesToRead = std::min((uint64_t)n, buf.size() - pos);
              if (bytesToRead > 0) {
                  std::memcpy(b, buf.data() + pos, bytesToRead);
                  pos += bytesToRead;
              }
              if (readBytes) *readBytes = (Steinberg::int32)bytesToRead;
              return Steinberg::kResultOk;
          }
          Steinberg::tresult PLUGIN_API write(void* b, Steinberg::int32 n, Steinberg::int32* written) override {
              uint64_t currentSize = buf.size();
              buf.resize(currentSize + n);
              std::memcpy(buf.data() + pos, b, n);
              pos += n;
              if (written) *written = n;
              return Steinberg::kResultOk;
          }
          Steinberg::tresult PLUGIN_API seek(Steinberg::int64 p, Steinberg::int32 mode, Steinberg::int64* result) override {
              switch (mode) {
                  case kIBSeekSet: pos = (uint64_t)p; break;
                  case kIBSeekCur: pos = (uint64_t)((int64_t)pos + p); break;
                  case kIBSeekEnd: pos = (uint64_t)((int64_t)buf.size() + p); break;
              }
              if (result) *result = (Steinberg::int64)pos;
              return Steinberg::kResultOk;
          }
          Steinberg::tresult PLUGIN_API tell(Steinberg::int64* p) override { if (p) *p = (Steinberg::int64)pos; return Steinberg::kResultOk; }
          Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
          Steinberg::uint32 PLUGIN_API release() override { return 1; }
          Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
              if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::IBStream::iid)) { *obj = this; return Steinberg::kResultOk; }
              return Steinberg::kNoInterface;
          }
          std::vector<uint8_t> buf;
          uint64_t pos = 0;
      };
      SyncStateStream stream;
      if (m_component->getState(&stream) == Steinberg::kResultOk) {
          stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
          m_controller->setComponentState(&stream);
      }
  }

  std::memset(&m_vstContext, 0, sizeof(m_vstContext));
  std::memset(&m_processData, 0, sizeof(m_processData));
  m_processData.processContext = &m_vstContext;
  m_processData.inputEvents = m_inputEvents;
  m_processData.outputEvents = m_outputEvents;
  m_processData.inputParameterChanges = m_inputParams;
  m_processData.outputParameterChanges = m_outputParams;
  m_processData.processMode = Steinberg::Vst::kRealtime;
  m_processData.symbolicSampleSize = Steinberg::Vst::kSample32;

  // Negotiate speaker arrangement and setup bus layouts
  setupBuffers(0, 0);

  // Pre-initialize processing setup and active state on NRT thread
  Steinberg::Vst::ProcessSetup setup;
  setup.processMode = Steinberg::Vst::kRealtime;
  setup.symbolicSampleSize = Steinberg::Vst::kSample32;
  setup.maxSamplesPerBlock = 512;
  setup.sampleRate = 44100.0;
  
  m_processor->setupProcessing(setup);
  
  m_component->setActive(true);
  
  m_processor->setProcessing(true);
  
  m_isInitialized = true;
  m_currentSampleRate = 44100.0;
  m_currentMaxBlockSize = 512;
 
  // Pre-allocate dynamic multi-bus buffers based on actual plugin layout

  if (m_numInputBuses > 0) {
      m_vstInputs = new ::Steinberg::Vst::AudioBusBuffers[m_numInputBuses];
      for (uint32_t i = 0; i < m_numInputBuses; ++i) {
          uint32_t channelCount = m_inputBusChannels[i];
          m_vstInputs[i].numChannels = (Steinberg::int32)channelCount;
          m_vstInputs[i].silenceFlags = 0;
          m_vstInputs[i].channelBuffers32 = new float*[channelCount];
          for (uint32_t ch = 0; ch < channelCount; ++ch) {
              m_vstInputs[i].channelBuffers32[ch] = nullptr;
          }
      }
  } else {
      m_vstInputs = nullptr;
  }

  if (m_numOutputBuses > 0) {
      m_vstOutputs = new ::Steinberg::Vst::AudioBusBuffers[m_numOutputBuses];
      for (uint32_t i = 0; i < m_numOutputBuses; ++i) {
          uint32_t channelCount = m_outputBusChannels[i];
          m_vstOutputs[i].numChannels = (Steinberg::int32)channelCount;
          m_vstOutputs[i].silenceFlags = 0;
          m_vstOutputs[i].channelBuffers32 = new float*[channelCount];
          for (uint32_t ch = 0; ch < channelCount; ++ch) {
              m_vstOutputs[i].channelBuffers32[ch] = nullptr;
          }
      }
  } else {
      m_vstOutputs = nullptr;
  }

  m_processData.inputs = m_vstInputs;
  m_processData.numInputs = (Steinberg::int32)m_numInputBuses;
  m_processData.outputs = m_vstOutputs;
  m_processData.numOutputs = (Steinberg::int32)m_numOutputBuses;

  // Initialize MIDI CC cache with kNoParamId
  for (uint32_t b = 0; b < MAX_SUPPORTED_MIDI_BUSES; ++b) {
      for (int ch = 0; ch < 16; ++ch) {
          for (int cc = 0; cc < 129; ++cc) {
              m_midiCCCache[b][ch][cc] = Steinberg::Vst::kNoParamId;
          }
      }
  }

  // Cache MIDI assignments on the NRT load thread
  cacheMidiMappings();

  return true;
}

void VST3PluginImpl::cacheMidiMappings() {
  if (!m_midiMapping || !m_component) return;

  using namespace Steinberg;
  using namespace Steinberg::Vst;

  int32 midiBusCount = m_component->getBusCount(kEvent, kInput);
  m_numMidiInputBuses = std::min(static_cast<uint32_t>(midiBusCount), 
                                 static_cast<uint32_t>(MAX_SUPPORTED_MIDI_BUSES));

  for (uint32_t b = 0; b < m_numMidiInputBuses; ++b) {
    for (int ch = 0; ch < 16; ++ch) {
      for (int cc = 0; cc < 129; ++cc) {
        ParamID paramId = kNoParamId;
        int32 ccNum = (cc == 128) ? ControllerNumbers::kPitchBend : cc;
        if (m_midiMapping->getMidiControllerAssignment(b, ch, ccNum, paramId) == kResultOk) {
          m_midiCCCache[b][ch][cc] = paramId;
        } else {
          m_midiCCCache[b][ch][cc] = kNoParamId;
        }
      }
    }
  }
}

void VST3PluginImpl::unload() {
  if (m_processor) {
      m_processor->setProcessing(false);
      m_component->setActive(false);
  }
  
  if (m_component && m_controller) {
      Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> cpComponent(m_component);
      Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> cpController(m_controller);
      if (cpComponent && cpController) {
          cpComponent->disconnect(cpController);
          cpController->disconnect(cpComponent);
      }
  }

  if (m_vstInputs) {
    for (uint32_t i = 0; i < m_numInputBuses; ++i) {
      delete[] m_vstInputs[i].channelBuffers32;
    }
    delete[] m_vstInputs;
    m_vstInputs = nullptr;
  }
  if (m_vstOutputs) {
    for (uint32_t i = 0; i < m_numOutputBuses; ++i) {
      delete[] m_vstOutputs[i].channelBuffers32;
    }
    delete[] m_vstOutputs;
    m_vstOutputs = nullptr;
  }

  if (m_component) {
    m_component->terminate();
    m_component = nullptr;
  }
  m_processor = nullptr;
  m_controller = nullptr;
  m_midiMapping = nullptr;

  if (m_module) {
#if defined(__APPLE__)
    if (m_bundleRef) {
      auto bundleExit = (BundleExitProc)dlsym(m_module, "bundleExit");
      if (bundleExit) {
        bundleExit();
      }
      CFRelease(static_cast<CFBundleRef>(m_bundleRef));
      m_bundleRef = nullptr;
    }
#endif
    auto exitDll = (ExitDllProc)dlsym(m_module, "ExitDll");
    if (exitDll) {
        exitDll();
    }
    dlclose(m_module);
    m_module = nullptr;
  }
}

bool VST3PluginImpl::getInfo(PluginInfo &outInfo) const {
  if (!m_component) return false;
  outInfo.numInputs = (uint32_t)(m_component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput) * 2); 
  outInfo.numOutputs = (uint32_t)(m_component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput) * 2);
  outInfo.numParameters = (uint32_t)(m_controller ? m_controller->getParameterCount() : 0);
  outInfo.hasEditor = (m_controller != nullptr);
  
  uint32_t midiInCount = (uint32_t)m_component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
  uint32_t audioOutCount = (uint32_t)m_component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
  outInfo.isInstrument = (midiInCount > 0 && audioOutCount > 0);

  std::strncpy(outInfo.name, m_name, sizeof(outInfo.name) - 1);
  outInfo.name[sizeof(outInfo.name) - 1] = '\0';
  
  return true;
}

void VST3PluginImpl::processAudio(float *const *inputs, uint32_t numInputs,
                                  float *const *outputs, uint32_t numOutputs,
                                  uint32_t numSamples, const EventData *events,
                                  uint32_t numEvents,
                                  EventData *outEvents, uint32_t *outCount,
                                  const ::ProcessContext *context,
                                  const bool* inputSilence) {
  if (!m_processor) return;

  // Gate real-time processing during asynchronous state/soundbank loading.
  // This prevents the audio thread from calling process() concurrently with setState(),
  // eliminating catastrophic access violation crashes.
  if (m_isStateLoading.load(std::memory_order_acquire)) {
      for (uint32_t ch = 0; ch < numOutputs; ++ch) {
          if (outputs[ch]) {
              std::memset(outputs[ch], 0, numSamples * sizeof(float));
          }
      }
      if (outCount) *outCount = 0;
      return;
  }

  struct ProcessingGuard {
      std::atomic<bool>& flag;
      ProcessingGuard(std::atomic<bool>& f) : flag(f) {
          flag.store(true, std::memory_order_release);
      }
      ~ProcessingGuard() {
          flag.store(false, std::memory_order_release);
      }
  } guard(m_isProcessing);

  // 1. Ensure plugin is initialized. Setup changes must be executed NRT prior to playback.
  if (!m_isInitialized || !context) {
      return;
  }

  // 2. Map Process Context
  setupProcessContext(context);

  // 3. Map Audio Buffers (Zero-Allocation, Multi-Bus Safe)
  m_processData.numSamples = (Steinberg::int32)numSamples;
  
  m_processData.inputs = (m_numInputBuses > 0) ? m_vstInputs : nullptr;
  m_processData.numInputs = (Steinberg::int32)m_numInputBuses;

  m_processData.outputs = (m_numOutputBuses > 0) ? m_vstOutputs : nullptr;
  m_processData.numOutputs = (Steinberg::int32)m_numOutputBuses;

  size_t silentIdx = 0;
  uint32_t flatChannelIdx = 0;
  for (uint32_t b = 0; b < m_numInputBuses; ++b) {
      uint32_t busChannels = m_inputBusChannels[b];
      uint32_t busSilenceFlags = 0;
      for (uint32_t ch = 0; ch < busChannels; ++ch) {
          bool isChannelSilent = false;
          if (inputs && flatChannelIdx < numInputs) {
              if (inputSilence && inputSilence[flatChannelIdx]) {
                  isChannelSilent = true;
              }
              if (inputs[flatChannelIdx] != nullptr) {
                  m_vstInputs[b].channelBuffers32[ch] = const_cast<float*>(inputs[flatChannelIdx++]);
              } else {
                  size_t idx = silentIdx < MAX_SILENT_CHANNELS ? silentIdx++ : (MAX_SILENT_CHANNELS - 1);
                  m_vstInputs[b].channelBuffers32[ch] = m_silentBuffers[idx];
                  flatChannelIdx++;
                  isChannelSilent = true;
              }
          } else {
              size_t idx = silentIdx < MAX_SILENT_CHANNELS ? silentIdx++ : (MAX_SILENT_CHANNELS - 1);
              m_vstInputs[b].channelBuffers32[ch] = m_silentBuffers[idx];
              isChannelSilent = true;
          }
          if (isChannelSilent) {
              busSilenceFlags |= (1 << ch);
          }
      }
      m_vstInputs[b].silenceFlags = busSilenceFlags;
  }

  flatChannelIdx = 0;
  for (uint32_t b = 0; b < m_numOutputBuses; ++b) {
      uint32_t busChannels = m_outputBusChannels[b];
      for (uint32_t ch = 0; ch < busChannels; ++ch) {
          if (outputs && flatChannelIdx < numOutputs && outputs[flatChannelIdx] != nullptr) {
              m_vstOutputs[b].channelBuffers32[ch] = outputs[flatChannelIdx++];
          } else {
              size_t idx = silentIdx < MAX_SILENT_CHANNELS ? silentIdx++ : (MAX_SILENT_CHANNELS - 1);
              m_vstOutputs[b].channelBuffers32[ch] = m_silentBuffers[idx];
          }
      }
  }

  // 4. Map Events
  auto* event_list = static_cast<EventList*>(m_inputEvents.get());
  auto* param_changes = static_cast<ParameterChanges*>(m_inputParams.get());
  event_list->clear();
  if (m_outputEvents) {
      static_cast<EventList*>(m_outputEvents.get())->clear();
  }

  for (uint32_t i = 0; i < numEvents; ++i) {
      const auto& ev = events[i];
      
      switch (ev.eventType) {
          case EventType::MIDI_NOTE_ON: {
              Steinberg::Vst::Event vst_ev;
              std::memset(&vst_ev, 0, sizeof(vst_ev));
              vst_ev.sampleOffset = static_cast<Steinberg::int32>(ev.sampleOffset);
              vst_ev.type         = Steinberg::Vst::Event::kNoteOnEvent;
              vst_ev.flags        = 0; // kIsLive handled implicitly if needed, but safe to be 0
              vst_ev.noteOn.channel  = ev.payload.midiNote.channel;
              vst_ev.noteOn.pitch    = ev.payload.midiNote.pitch;
              vst_ev.noteOn.velocity = static_cast<float>(ev.payload.midiNote.velocity) / 127.0f;
              vst_ev.noteOn.noteId   = -1; // VST3 SDK: Set to -1 if host does not track unique note IDs
              vst_ev.noteOn.tuning   = 0.0f;
              vst_ev.noteOn.length   = 0;
              event_list->addEvent(vst_ev);
              break;
          }

          case EventType::MIDI_NOTE_OFF: {
              Steinberg::Vst::Event vst_ev;
              std::memset(&vst_ev, 0, sizeof(vst_ev));
              vst_ev.sampleOffset = static_cast<Steinberg::int32>(ev.sampleOffset);
              vst_ev.type         = Steinberg::Vst::Event::kNoteOffEvent;
              vst_ev.flags        = 0;
              vst_ev.noteOff.channel  = ev.payload.midiNote.channel;
              vst_ev.noteOff.pitch    = ev.payload.midiNote.pitch;
              vst_ev.noteOff.velocity = 0.0f;
              vst_ev.noteOff.noteId   = -1;
              vst_ev.noteOff.tuning   = 0.0f;
              event_list->addEvent(vst_ev);
              break;
          }

          case EventType::MIDI_CC: {
              uint8_t ch = ev.payload.midiCC.channel;
              uint8_t ccNum = ev.payload.midiCC.controllerNumber;
              if (ch < 16 && ccNum < 128) {
                  for (uint32_t b = 0; b < std::max(1u, m_numMidiInputBuses); ++b) {
                      Steinberg::Vst::ParamID param_id = m_midiCCCache[b][ch][ccNum];
                      if (param_id != Steinberg::Vst::kNoParamId) {
                          Steinberg::int32 index = 0;
                          auto* queue = param_changes->addParameterData(param_id, index);
                          if (queue) {
                              Steinberg::int32 point_index = 0;
                              float norm_value = static_cast<float>(ev.payload.midiCC.value) / 127.0f;
                              // Inject parameter modulation at the exact sample offset
                              queue->addPoint(static_cast<Steinberg::int32>(ev.sampleOffset), 
                                              static_cast<Steinberg::Vst::ParamValue>(norm_value), 
                                              point_index);
                          }
                      }
                  }
              }
              break;
          }

          case EventType::MIDI_PITCH: {
              uint8_t ch = ev.payload.midiPitch.channel;
              if (ch < 16) {
                  for (uint32_t b = 0; b < std::max(1u, m_numMidiInputBuses); ++b) {
                      Steinberg::Vst::ParamID param_id = m_midiCCCache[b][ch][128]; // Pitch Bend cached at index 128
                      if (param_id != Steinberg::Vst::kNoParamId) {
                          Steinberg::int32 index = 0;
                          auto* queue = param_changes->addParameterData(param_id, index);
                          if (queue) {
                              Steinberg::int32 point_index = 0;
                              // Map 0-16383 payload to 0.0-1.0 normalized value (8192 = 0.5 center)
                              float norm_value = static_cast<float>(ev.payload.midiPitch.value) / 16383.0f;
                              queue->addPoint(static_cast<Steinberg::int32>(ev.sampleOffset), 
                                              static_cast<Steinberg::Vst::ParamValue>(norm_value), 
                                              point_index);
                          }
                      }
                  }
              }
              break;
          }

          case EventType::AUTOMATION: {
              Steinberg::Vst::ParamID param_id = static_cast<Steinberg::Vst::ParamID>(ev.payload.automation.parameterIndex);
              Steinberg::int32 index = 0;
              auto* queue = param_changes->addParameterData(param_id, index);
              if (queue) {
                  Steinberg::int32 point_index = 0;
                  queue->addPoint(static_cast<Steinberg::int32>(ev.sampleOffset), 
                                  static_cast<Steinberg::Vst::ParamValue>(ev.payload.automation.targetValue), 
                                  point_index);
              }
              break;
          }
      }
  }

  // 5. Processing
  {
      Layer1::ScopedDenormalHandler denormalHandler;
      m_processor->process(m_processData);
  }

  // 6. Output Event Extraction
  if (m_processData.outputEvents && outEvents && outCount) {
      *outCount = 0;
      auto* out_list = m_processData.outputEvents;
      Steinberg::int32 num_events = out_list->getEventCount();
      
      // Clamp output to system limits to prevent buffer overruns
      constexpr Steinberg::int32 MAX_EVENTS_LIMIT = 512;
      if (num_events > MAX_EVENTS_LIMIT) {
          num_events = MAX_EVENTS_LIMIT;
      }
      
      for (Steinberg::int32 i = 0; i < num_events; ++i) {
          Steinberg::Vst::Event vst_ev;
          if (out_list->getEvent(i, vst_ev) == Steinberg::kResultOk) {
              EventData& sys_ev = outEvents[*outCount];
              sys_ev.sampleOffset = static_cast<uint32_t>(vst_ev.sampleOffset);
              sys_ev.targetNodeId = {}; // Filled by router later
              sys_ev.flags        = 0;
              sys_ev.padding      = 0;
              
              if (vst_ev.type == Steinberg::Vst::Event::kNoteOnEvent) {
                  sys_ev.eventType = EventType::MIDI_NOTE_ON;
                  sys_ev.payload.midiNote.pitch    = static_cast<uint8_t>(vst_ev.noteOn.pitch);
                  sys_ev.payload.midiNote.velocity = static_cast<uint8_t>(vst_ev.noteOn.velocity * 127.0f);
                  sys_ev.payload.midiNote.channel  = static_cast<uint8_t>(vst_ev.noteOn.channel);
                  (*outCount)++;
              } 
              else if (vst_ev.type == Steinberg::Vst::Event::kNoteOffEvent) {
                  sys_ev.eventType = EventType::MIDI_NOTE_OFF;
                  sys_ev.payload.midiNote.pitch    = static_cast<uint8_t>(vst_ev.noteOff.pitch);
                  sys_ev.payload.midiNote.velocity = static_cast<uint8_t>(vst_ev.noteOff.velocity * 127.0f);
                  sys_ev.payload.midiNote.channel  = static_cast<uint8_t>(vst_ev.noteOff.channel);
                  (*outCount)++;
              }
          }
      }
  }
  
  // Clear parameter changes after process
  param_changes->clear();
}

void VST3PluginImpl::setupProcessContext(const ::ProcessContext *context) {
  if (!context) return;
  m_vstContext.sampleRate = (double)context->sampleRate;
  m_vstContext.projectTimeSamples = (Steinberg::int64)context->transport.positionSample;
  m_vstContext.tempo = context->transport.bpm;
  m_vstContext.timeSigNumerator = (Steinberg::int32)context->transport.numerator;
  m_vstContext.timeSigDenominator = (Steinberg::int32)context->transport.denominator;
  m_vstContext.systemTime = (Steinberg::int64)context->hardwareTimestamp;

  if (context->transport.ticksPerBeat > 0) {
      double quarterNotesPerBeat = 4.0 / context->transport.denominator;
      double barStartQuarterNotes = (context->transport.bar > 0 ? context->transport.bar - 1 : 0) * context->transport.numerator * quarterNotesPerBeat;
      double currentBeatQuarterNotes = (context->transport.beat > 0 ? context->transport.beat - 1 : 0) * quarterNotesPerBeat;
      double tickQuarterNotes = ((double)context->transport.tick / context->transport.ticksPerBeat) * quarterNotesPerBeat;
      
      m_vstContext.projectTimeMusic = barStartQuarterNotes + currentBeatQuarterNotes + tickQuarterNotes;
      m_vstContext.barPositionMusic = barStartQuarterNotes;
  } else {
      m_vstContext.projectTimeMusic = 0.0;
      m_vstContext.barPositionMusic = 0.0;
  }

  m_vstContext.state = Steinberg::Vst::ProcessContext::kTempoValid | 
                       Steinberg::Vst::ProcessContext::kTimeSigValid |
                       Steinberg::Vst::ProcessContext::kSystemTimeValid |
                       Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
                       Steinberg::Vst::ProcessContext::kBarPositionValid;
                       
  if (context->transportState == TransportState::PLAYING) {
      m_vstContext.state |= Steinberg::Vst::ProcessContext::kPlaying;
  } else if (context->transportState == TransportState::RECORDING) {
      m_vstContext.state |= (Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kRecording);
  }
}

float VST3PluginImpl::getParameterValue(uint32_t paramIndex) const {
  if (!m_controller) return 0.0f;
  return (float)m_controller->getParamNormalized((Steinberg::Vst::ParamID)paramIndex);
}

void VST3PluginImpl::setParameterValue(uint32_t paramIndex, float value) {
  if (!m_controller) return;
  m_controller->setParamNormalized((Steinberg::Vst::ParamID)paramIndex, (Steinberg::Vst::ParamValue)value);
  
  // Add to input parameter changes for the next process cycle
  Steinberg::int32 index = 0;
  auto queue = m_inputParams->addParameterData((Steinberg::Vst::ParamID)paramIndex, index);
  if (queue) {
      Steinberg::int32 pointIndex = 0;
      queue->addPoint(0, (Steinberg::Vst::ParamValue)value, pointIndex);
  }
}

static void convertTCharToUTF8(const Steinberg::Vst::TChar* src, char* dst, size_t maxLen) {
    if (!src || !dst || maxLen == 0) return;
    size_t i = 0;
    for (; i < maxLen - 1 && src[i]; ++i) {
        uint32_t cp = src[i];
        if (cp < 0x80) {
            dst[i] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            if (i + 2 >= maxLen) break;
            dst[i++] = static_cast<char>((cp >> 6) | 0xC0);
            dst[i] = static_cast<char>((cp & 0x3F) | 0x80);
        } else {
            if (i + 3 >= maxLen) break;
            dst[i++] = static_cast<char>((cp >> 12) | 0xE0);
            dst[i++] = static_cast<char>(((cp >> 6) & 0x3F) | 0x80);
            dst[i] = static_cast<char>((cp & 0x3F) | 0x80);
        }
    }
    dst[i] = '\0';
}

bool VST3PluginImpl::getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const {
  if (!m_controller) return false;
  
  Steinberg::Vst::ParameterInfo vstInfo{};
  if (m_controller->getParameterInfo(static_cast<Steinberg::int32>(paramIndex), vstInfo) != Steinberg::kResultOk) {
    return false;
  }
  
  outInfo.index = vstInfo.id;
  convertTCharToUTF8(vstInfo.title, outInfo.name, sizeof(outInfo.name));
  convertTCharToUTF8(vstInfo.units, outInfo.unit, sizeof(outInfo.unit));
  
  outInfo.minValue = 0.0f;
  if (vstInfo.stepCount > 1) {
      outInfo.maxValue = static_cast<float>(vstInfo.stepCount);
  } else {
      outInfo.maxValue = 1.0f;
  }
  outInfo.defaultValue = static_cast<float>(vstInfo.defaultNormalizedValue);
  
  outInfo.flags = ::ParameterInfo::NONE;
  if (vstInfo.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_AUTOMATABLE);
  }
  if (vstInfo.flags & Steinberg::Vst::ParameterInfo::kIsReadOnly) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_READ_ONLY);
  }
  if (vstInfo.stepCount == 1) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
  } else if (vstInfo.stepCount > 1) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_INTEGER);
  }
  
  return true;
}

std::vector<uint8_t> VST3PluginImpl::getState() const {
    if (!m_component) return {};
    
    class MemoryStream : public Steinberg::IBStream {
    public:
        MemoryStream() : pos(0) {}
        Steinberg::tresult PLUGIN_API read(void*, Steinberg::int32, Steinberg::int32*) override { return Steinberg::kNotImplemented; }
        Steinberg::tresult PLUGIN_API write(void* b, Steinberg::int32 n, Steinberg::int32* written) override {
            uint32_t currentSize = buf.size();
            buf.resize(currentSize + n);
            std::memcpy(buf.data() + pos, b, n);
            pos += n;
            if (written) *written = n;
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API seek(Steinberg::int64, Steinberg::int32, Steinberg::int64*) override { return Steinberg::kNotImplemented; }
        Steinberg::tresult PLUGIN_API tell(Steinberg::int64* p) override { *p = (Steinberg::int64)pos; return Steinberg::kResultOk; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
            if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::IBStream::iid)) { *obj = this; return Steinberg::kResultOk; }
            return Steinberg::kNoInterface;
        }
        std::vector<uint8_t> buf;
    private:
        uint64_t pos;
    };

    MemoryStream stream;
    if (m_component->getState(&stream) == Steinberg::kResultOk) {
        return stream.buf;
    }
    return {};
}

bool VST3PluginImpl::loadState(const uint8_t *buffer, uint64_t bufferSize) {
  if (!m_component) return false;

  auto stateData = std::make_shared<std::vector<uint8_t>>(buffer, buffer + bufferSize);

  auto loadTask = [this, stateData]() {
    // 1. Force state loading lock (atomic)
    m_isStateLoading.store(true, std::memory_order_release);

    // 2. Safely wait for active RT processing block to complete.
    while (m_isProcessing.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // 3. Perform state recovery synchronously on the butler/NRT thread
    Steinberg::VstMemoryStream stream(stateData->data(), stateData->size());
    Steinberg::tresult res = m_component->setState(&stream);
    (void)res;

    // 4. Resume audio processing after memory mutation completes
    m_isStateLoading.store(false, std::memory_order_release);
  };

  if (m_butler) { 
    m_isStateLoading = true; 
    m_butler->scheduleTask(loadTask); 
    return true; 
  } else { 
    loadTask(); 
    return true; 
  }
}

void VST3PluginImpl::setupBuffers(uint32_t numInputs, uint32_t numOutputs) {
    (void)numInputs;
    (void)numOutputs;
    if (!m_component || !m_processor) return;

    using namespace Steinberg;
    using namespace Steinberg::Vst;

    int32 inputBusCount = m_component->getBusCount(kAudio, kInput);
    m_numInputBuses = static_cast<uint32_t>(inputBusCount);
    m_inputBusChannels.resize(m_numInputBuses);

    std::vector<SpeakerArrangement> inputArrangements(inputBusCount, SpeakerArr::kStereo);
    for (int32 b = 0; b < inputBusCount; ++b) {
        BusInfo busInfo;
        if (m_component->getBusInfo(kAudio, kInput, b, busInfo) == kResultOk) {
            inputArrangements[b] = (busInfo.channelCount == 1) ? SpeakerArr::kMono : SpeakerArr::kStereo;
        }
    }

    int32 outputBusCount = m_component->getBusCount(kAudio, kOutput);
    m_numOutputBuses = static_cast<uint32_t>(outputBusCount);
    m_outputBusChannels.resize(m_numOutputBuses);

    std::vector<SpeakerArrangement> outputArrangements(outputBusCount, SpeakerArr::kStereo);

    // Call setBusArrangements to negotiate layouts with the plugin
    m_processor->setBusArrangements(inputArrangements.data(), inputBusCount, outputArrangements.data(), outputBusCount);

    // Now activate all buses and get final negotiated layouts
    for (int32 b = 0; b < inputBusCount; ++b) {
        m_component->activateBus(kAudio, kInput, b, true);
        
        // Query the finalized channel count from the bus
        BusInfo busInfo;
        if (m_component->getBusInfo(kAudio, kInput, b, busInfo) == kResultOk) {
            m_inputBusChannels[b] = busInfo.channelCount;
        } else {
            m_inputBusChannels[b] = (inputArrangements[b] == SpeakerArr::kMono) ? 1 : 2;
        }
    }

    for (int32 b = 0; b < outputBusCount; ++b) {
        m_component->activateBus(kAudio, kOutput, b, true);
        
        BusInfo busInfo;
        if (m_component->getBusInfo(kAudio, kOutput, b, busInfo) == kResultOk) {
            m_outputBusChannels[b] = busInfo.channelCount;
        } else {
            m_outputBusChannels[b] = 2; // Default fallback to Stereo
        }
    }
}

// ... rest of the file (Editor methods and HostContext) remains aligned ...
Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::queryInterface(const Steinberg::TUID _iid, void **obj) {
  if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
    *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
    addRef();
    return Steinberg::kResultOk;
  }
  if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IComponentHandler::iid)) {
    *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
    addRef();
    return Steinberg::kResultOk;
  }
  if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IHostApplication::iid)) {
    *obj = static_cast<Steinberg::Vst::IHostApplication*>(this);
    addRef();
    return Steinberg::kResultOk;
  }
  if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::IPlugFrame::iid)) {
    *obj = static_cast<Steinberg::IPlugFrame*>(this);
    addRef();
    return Steinberg::kResultOk;
  }
  *obj = nullptr;
  return Steinberg::kNoInterface;
}

Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::beginEdit(Steinberg::Vst::ParamID id) { (void)id; return Steinberg::kResultOk; }
Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) { 
  if (m_owner && m_owner->m_tweakedCallback) {
    m_owner->m_tweakedCallback(static_cast<uint32_t>(id), static_cast<float>(value));
  }
  return Steinberg::kResultOk; 
}
Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::endEdit(Steinberg::Vst::ParamID id) { (void)id; return Steinberg::kResultOk; }
Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::restartComponent(Steinberg::int32 flags) { (void)flags; return Steinberg::kResultOk; }

Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::getName(Steinberg::Vst::String128 name) {
  std::string hostName = std::string(config::PROJECT_DISPLAY_NAME) + " Host";
  size_t len = std::min<size_t>(hostName.size(), 127);
  for (size_t i = 0; i < len; ++i) {
    name[i] = static_cast<Steinberg::Vst::TChar>(hostName[i]);
  }
  name[len] = 0;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void **obj) {
  (void)cid;
  
  if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IMessage::iid)) {
      if (obj) *obj = new Steinberg::Vst::HostMessage();
      return Steinberg::kResultOk;
  }
  if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IAttributeList::iid)) {
      if (obj) {
          auto attr = Steinberg::Vst::HostAttributeList::make();
          attr->addRef();
          *obj = attr.get();
      }
      return Steinberg::kResultOk;
  }
  if (obj) *obj = nullptr;
  return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3PluginImpl::HostContext::resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) {
  if (view && newSize) {
    return view->onSize(newSize);
  }
  return Steinberg::kResultFalse;
}

bool VST3PluginImpl::openEditor(void *parentWindow, int &outWidth, int &outHeight) {
  if (!m_controller) {
    return false;
  }
  if (m_view) {
    closeEditor();
  }
  
  m_view = m_controller->createView(Steinberg::Vst::ViewType::kEditor);
  if (!m_view) {
    return false;
  }

  m_view->setFrame(static_cast<Steinberg::IPlugFrame*>(m_hostContext.get()));

  // Retrieve preferred view size before attaching
  Steinberg::ViewRect rect{};
  if (m_view->getSize(&rect) == Steinberg::kResultOk || m_view->getSize(&rect) == 1) {
    outWidth = rect.right - rect.left;
    outHeight = rect.bottom - rect.top;
  } else {
    outWidth = 640;
    outHeight = 480;
  }

  const char* platform = nullptr;
#if defined(_WIN32) || defined(__WIN32__)
  platform = Steinberg::kPlatformTypeHWND;
#elif defined(__APPLE__)
  platform = Steinberg::kPlatformTypeNSView;
#else
  #error "Current Operating System is not supported for VST3 editor window parenting!"
#endif
  Steinberg::tresult res = m_view->attached(parentWindow, platform);
  if (res != Steinberg::kResultOk) { m_view = nullptr; return false; }
  return true;
}

void VST3PluginImpl::closeEditor() {
  if (m_view) { m_view->removed(); m_view = nullptr; }
}

} // namespace Layer3
