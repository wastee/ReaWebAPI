#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <thread>

// Test-only hardware callback observer. The callback never allocates or locks.
struct JsfxAudioMeasurement {
  using Clock=std::chrono::steady_clock;
  struct Sample {double processing_ms=0,interval_ms=0,budget_ms=0;};
  std::array<Sample,262144> samples{};
  std::atomic<bool> enabled{false};
  std::atomic<unsigned> readers{0};
  size_t count=0,overflow=0;
  Clock::time_point before{},previous{};
  audio_hook_register_t hook{};
  int (*reg)(bool,audio_hook_register_t*)=nullptr;
  void (*xrun)(unsigned*,unsigned*,unsigned*)=nullptr;
  unsigned audio_begin=0,media_begin=0,time_begin=0;
  static void callback(bool post,int length,double rate,audio_hook_register_t* hook) {
    auto& self=*static_cast<JsfxAudioMeasurement*>(hook->userdata1);
    self.readers.fetch_add(1);
    if(self.enabled.load()) {
      const auto now=Clock::now();
      if(!post)self.before=now;
      else if(self.before!=Clock::time_point{}) {
        const Sample sample{std::chrono::duration<double,std::milli>(now-self.before).count(),
          self.previous==Clock::time_point{}?0:std::chrono::duration<double,std::milli>(now-self.previous).count(),
          1000.0*length/rate};
        self.previous=now;
        self.before={};
        if(self.count<self.samples.size())self.samples[self.count++]=sample;else ++self.overflow;
      }
    }
    self.readers.fetch_sub(1);
  }
  void begin(reaper_plugin_info_t* host) {
    reg=reinterpret_cast<decltype(reg)>(host->GetFunc("Audio_RegHardwareHook"));
    xrun=reinterpret_cast<decltype(xrun)>(host->GetFunc("GetUnderrunTime"));
    if(!reg || !xrun)throw std::runtime_error("Missing audio measurement APIs");
    if(!hook.OnAudioBuffer) {
      hook.OnAudioBuffer=callback;hook.userdata1=this;
      if(!reg(true,&hook)){hook.OnAudioBuffer=nullptr;throw std::runtime_error("Audio hook rejected");}
    }
    enabled=false;while(readers.load())std::this_thread::yield();
    count=overflow=0;before=previous={};xrun(&audio_begin,&media_begin,&time_begin);enabled=true;
  }
  static reaweb::Json distribution(std::vector<double> values) {
    if(values.empty())return nullptr;
    std::sort(values.begin(),values.end());
    return {{"count",values.size()},{"p50",values[size_t((values.size()-1)*.50)]},
      {"p95",values[size_t((values.size()-1)*.95)]},{"p99",values[size_t((values.size()-1)*.99)]},
      {"max",values.back()},{"min",values.front()}};
  }
  reaweb::Json end() {
    enabled=false;while(readers.load())std::this_thread::yield();
    unsigned audio=0,media=0,now=0;xrun(&audio,&media,&now);
    std::vector<double> processing,interval,budget;size_t exceeded=0;
    for(size_t i=0;i<count;++i) {
      processing.push_back(samples[i].processing_ms);budget.push_back(samples[i].budget_ms);
      if(samples[i].interval_ms>0)interval.push_back(samples[i].interval_ms);
      if(samples[i].processing_ms>samples[i].budget_ms)++exceeded;
    }
    return {{"processingMs",distribution(processing)},{"intervalMs",distribution(interval)},
      {"budgetMs",distribution(budget)},{"overBudgetCallbacks",exceeded},{"overflow",overflow},
      {"audioXrunTimestampBefore",audio_begin},{"audioXrunTimestampAfter",audio},
      {"mediaXrunTimestampBefore",media_begin},{"mediaXrunTimestampAfter",media},
      {"audioXrunChanged",audio!=audio_begin},{"mediaXrunChanged",media!=media_begin},
      {"elapsedMs",unsigned(now-time_begin)}};
  }
  void close() {enabled=false;if(hook.OnAudioBuffer){reg(false,&hook);hook.OnAudioBuffer=nullptr;}}
};
