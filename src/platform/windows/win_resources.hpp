#pragma once
#include "web/web_resources.hpp"
#include <wrl.h>
#include <WebView2.h>
#include <cstring>
#include <mutex>

namespace reaweb {
class ResourceStream final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IStream, Microsoft::WRL::FtmBase> {
  ResourceResponse response_;
  uint64_t position_ = 0;
  std::mutex mutex_;
public:
  explicit ResourceStream(ResourceResponse response) : response_(std::move(response)) {}
  HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG count, ULONG* read) override {
    if (!buffer && count) return STG_E_INVALIDPOINTER;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto size = static_cast<ULONG>(std::min<uint64_t>(count, response_.size - std::min<uint64_t>(position_, response_.size)));
    if (size) std::memcpy(buffer, response_.data + position_, size);
    position_ += size; if (read) *read = size;
    return size == count ? S_OK : S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* position) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
    const uint64_t base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? position_ : response_.size;
    if ((offset.QuadPart < 0 && static_cast<uint64_t>(-(offset.QuadPart + 1)) + 1 > base) ||
        (offset.QuadPart > 0 && base > UINT64_MAX - offset.QuadPart)) return STG_E_INVALIDFUNCTION;
    position_ = base + offset.QuadPart;
    if (position) position->QuadPart = position_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
    if (!stat) return STG_E_INVALIDPOINTER;
    *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = response_.size; stat->grfMode = STGM_READ;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IStream** result) override {
    if (!result) return E_POINTER;
    std::lock_guard<std::mutex> lock(mutex_);
    auto clone = Microsoft::WRL::Make<ResourceStream>(response_);
    if (!clone) return E_OUTOFMEMORY;
    clone->position_ = position_; return clone.CopyTo(result);
  }
  HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
};
}
