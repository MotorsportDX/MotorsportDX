/**
 * @file        graphics/d3d12/dxil_compiler.cpp
 * @brief       DXC for the native path's DXIL shaders (docs/analysis/d3d-hle-n6.md 19.3.9)
 */

#include <rex/graphics/d3d12/dxil_compiler.h>

#include <cstring>
#include <filesystem>

#include <fmt/format.h>
#include <rex/filesystem.h>
#include <rex/hash.h>
#include <rex/logging.h>

namespace rex::graphics::d3d12 {

namespace {

std::wstring Wide(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring wide(size_t(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), size);
  return wide;
}

std::string Narrow(const wchar_t* text) {
  int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string narrow(size_t(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), size, nullptr, nullptr);
  return narrow;
}

// DXC's include interface over FXC's, so the overrides' include handler (the
// library by name, the look directories, the files read for the cache) serves
// both compilers. A stack object: the reference count is not used.
class IncludeAdapter : public IDxcIncludeHandler {
 public:
  IncludeAdapter(ID3DInclude* include, IDxcUtils* utils) : include_(include), utils_(utils) {}

  HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR file_name, IDxcBlob** blob_out) override {
    *blob_out = nullptr;
    std::string name = Narrow(file_name);
    // DXC names an include relative to the including file's directory.
    while (name.size() > 2 && name[0] == '.' && (name[1] == '/' || name[1] == '\\')) {
      name.erase(0, 2);
    }
    LPCVOID data = nullptr;
    UINT bytes = 0;
    if (!include_ || FAILED(include_->Open(D3D_INCLUDE_LOCAL, name.c_str(), nullptr, &data,
                                           &bytes))) {
      return E_FAIL;
    }
    IDxcBlobEncoding* blob = nullptr;
    HRESULT result = utils_->CreateBlob(data, bytes, DXC_CP_UTF8, &blob);
    include_->Close(data);
    if (FAILED(result)) {
      return result;
    }
    *blob_out = blob;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
    if (id == __uuidof(IDxcIncludeHandler) || id == __uuidof(IUnknown)) {
      *object = this;
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

 private:
  ID3DInclude* include_;
  IDxcUtils* utils_;
};

}  // namespace

std::unique_ptr<DxilCompiler> DxilCompiler::Load() {
  wchar_t module_path[MAX_PATH];
  DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    return nullptr;
  }
  std::filesystem::path directory = std::filesystem::path(module_path).parent_path();
  std::unique_ptr<DxilCompiler> compiler(new DxilCompiler());
  // dxil.dll first, from the same directory, so DXC signs with the release's
  // validator and not with one found elsewhere on the search path.
  compiler->dxil_ = LoadLibraryExW((directory / L"dxil.dll").c_str(), nullptr,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
  compiler->dxcompiler_ = LoadLibraryExW((directory / L"dxcompiler.dll").c_str(), nullptr,
                                         LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!compiler->dxil_ || !compiler->dxcompiler_) {
    REXGPU_INFO("DXIL shaders: no {} beside the executable; shaders compile with FXC",
                compiler->dxcompiler_ ? "dxil.dll" : "dxcompiler.dll");
    return nullptr;
  }
  compiler->create_instance_ = reinterpret_cast<DxcCreateInstanceProc>(
      GetProcAddress(compiler->dxcompiler_, "DxcCreateInstance"));
  IDxcCompiler3* dxc = nullptr;
  if (!compiler->create_instance_ ||
      FAILED(compiler->create_instance_(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc)))) {
    REXGPU_WARN("DXIL shaders: dxcompiler.dll does not create a compiler; shaders compile with "
                "FXC");
    return nullptr;
  }
  uint32_t identity[4] = {};
  IDxcVersionInfo2* info = nullptr;
  if (SUCCEEDED(dxc->QueryInterface(IID_PPV_ARGS(&info)))) {
    char* hash = nullptr;
    info->GetVersion(&identity[0], &identity[1]);
    info->GetCommitInfo(&identity[2], &hash);
    compiler->version_ = fmt::format("{}.{} ({} commits, {})", identity[0], identity[1],
                                     identity[2], hash ? hash : "no commit hash");
    if (hash) {
      identity[3] = uint32_t(XXH3_64bits(hash, std::strlen(hash)));
      CoTaskMemFree(hash);
    }
    info->Release();
  } else {
    compiler->version_ = "unknown";
  }
  dxc->Release();
  compiler->identity_ = XXH3_64bits(identity, sizeof(identity));
  REXGPU_INFO("DXIL shaders: DXC {} from {}", compiler->version_,
              rex::path_to_utf8(directory));
  return compiler;
}

DxilCompiler::~DxilCompiler() {
  if (dxcompiler_) {
    FreeLibrary(dxcompiler_);
  }
  if (dxil_) {
    FreeLibrary(dxil_);
  }
}

bool DxilCompiler::Compile(std::string_view source, const std::string& name, const char* target,
                           const std::vector<std::pair<std::string, std::string>>& defines,
                           ID3DInclude* include, bool strict, std::vector<uint8_t>& binary_out,
                           std::string& messages_out) const {
  binary_out.clear();
  messages_out.clear();
  IDxcCompiler3* dxc = nullptr;
  IDxcUtils* utils = nullptr;
  if (FAILED(create_instance_(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc))) ||
      FAILED(create_instance_(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
    if (dxc) {
      dxc->Release();
    }
    messages_out = "DXC: cannot create a compiler";
    return false;
  }
  std::vector<std::wstring> strings = {Wide(name), L"-E", L"main", L"-T", Wide(target),
                                       L"-HV", L"2018", L"-O3"};
  if (strict) {
    strings.push_back(DXC_ARG_IEEE_STRICTNESS);
  }
  for (const auto& define : defines) {
    strings.push_back(L"-D");
    strings.push_back(Wide(define.first + "=" + define.second));
  }
  std::vector<LPCWSTR> arguments;
  for (const std::wstring& argument : strings) {
    arguments.push_back(argument.c_str());
  }
  DxcBuffer buffer = {source.data(), source.size(), DXC_CP_UTF8};
  IncludeAdapter adapter(include, utils);
  IDxcResult* result = nullptr;
  HRESULT status = dxc->Compile(&buffer, arguments.data(), UINT32(arguments.size()), &adapter,
                                IID_PPV_ARGS(&result));
  bool compiled = false;
  if (SUCCEEDED(status) && result) {
    IDxcBlobUtf8* errors = nullptr;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors) {
      messages_out.assign(errors->GetStringPointer(), errors->GetStringLength());
      errors->Release();
    }
    result->GetStatus(&status);
    IDxcBlob* object = nullptr;
    if (SUCCEEDED(status) &&
        SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) && object) {
      const uint8_t* bytes = static_cast<const uint8_t*>(object->GetBufferPointer());
      binary_out.assign(bytes, bytes + object->GetBufferSize());
      object->Release();
      compiled = !binary_out.empty();
    }
    result->Release();
  }
  utils->Release();
  dxc->Release();
  return compiled;
}

}  // namespace rex::graphics::d3d12
