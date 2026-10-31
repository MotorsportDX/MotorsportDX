// The D3D12 ABI probe of the llvm-mingw prototype (docs/analysis/mingw-toolchain.md, section 8):
// the D3D12 methods that return a struct, called on a WARP device (CPU only), with their results
// printed as key=value lines. Built once against Microsoft's libraries and once with llvm-mingw,
// with and without WIDL_EXPLICIT_AGGREGATE_RETURNS; the lines must be equal. Microsoft's x64 ABI
// returns these structs through a hidden pointer, the GNU C++ ABI in registers (or through a
// hidden pointer passed before this). mingw-w64's _mingw_mac.h defines the macro for every GCC
// and clang C++ build; PROBE_GNU_RETURNS takes it away again to show what it prevents.
#include <cinttypes>
#include <cstdio>

#if defined(PROBE_GNU_RETURNS)
#include <_mingw_mac.h>
#undef WIDL_EXPLICIT_AGGREGATE_RETURNS
#endif

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static int failures = 0;

#define CHECK(expression)                                                        \
  do {                                                                           \
    HRESULT result_ = (expression);                                              \
    if (FAILED(result_)) {                                                       \
      std::printf("FAILED %s: 0x%08lX\n", #expression, (unsigned long)result_);   \
      return 1;                                                                  \
    }                                                                            \
  } while (0)

static void expect(const char* name, bool ok) {
  if (!ok) {
    std::printf("MISMATCH %s\n", name);
    ++failures;
  }
}

int main() {
  ComPtr<IDXGIFactory4> factory;
  CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  ComPtr<IDXGIAdapter> warp;
  CHECK(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

  // Two heaps: a shader-visible one has a GPU start, an RTV heap does not (GPU start 0).
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 1234;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heap_desc.NodeMask = 1;
  ComPtr<ID3D12DescriptorHeap> heap;
  CHECK(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)));
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = 7;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  CHECK(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)));

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_DESCRIPTOR_HEAP_DESC got_heap = heap->GetDesc();
  D3D12_DESCRIPTOR_HEAP_DESC got_rtv = rtv_heap->GetDesc();
  UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  // The addresses differ from run to run; whether they are set and aligned does not.
  std::printf("heap.cpu.nonzero=%d\n", cpu.ptr != 0);
  std::printf("heap.cpu.aligned=%d\n", cpu.ptr % 8 == 0);
  std::printf("heap.gpu.nonzero=%d\n", gpu.ptr != 0);
  std::printf("rtv.cpu.nonzero=%d\n", rtv_cpu.ptr != 0);
  std::printf("heap.desc=%d,%u,%d,%u\n", got_heap.Type, got_heap.NumDescriptors, got_heap.Flags,
              got_heap.NodeMask);
  std::printf("rtv.desc=%d,%u,%d,%u\n", got_rtv.Type, got_rtv.NumDescriptors, got_rtv.Flags,
              got_rtv.NodeMask);
  std::printf("increment=%u\n", increment);
  expect("heap.desc", got_heap.Type == heap_desc.Type && got_heap.NumDescriptors == 1234 &&
                          got_heap.Flags == heap_desc.Flags && got_heap.NodeMask == 1);
  expect("rtv.desc", got_rtv.Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV && got_rtv.NumDescriptors == 7);
  expect("heap.cpu", cpu.ptr != 0);
  expect("heap.gpu", gpu.ptr != 0);
  expect("rtv.cpu", rtv_cpu.ptr != 0);
  // A handle used for a write lands in the heap: a null SRV at the second slot.
  D3D12_SHADER_RESOURCE_VIEW_DESC null_view = {};
  null_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  null_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  null_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  null_view.Texture2D.MipLevels = 1;
  D3D12_CPU_DESCRIPTOR_HANDLE second = {cpu.ptr + increment};
  device->CreateShaderResourceView(nullptr, &null_view, second);
  std::printf("device.removed=0x%08lX\n", (unsigned long)device->GetDeviceRemovedReason());

  D3D12_RESOURCE_DESC texture = {};
  texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture.Alignment = 0;
  texture.Width = 1280;
  texture.Height = 720;
  texture.DepthOrArraySize = 3;
  texture.MipLevels = 4;
  texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  texture.SampleDesc.Count = 1;
  texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_RESOURCE_ALLOCATION_INFO info = device->GetResourceAllocationInfo(0, 1, &texture);
  std::printf("allocation=%" PRIu64 ",%" PRIu64 "\n", (uint64_t)info.SizeInBytes,
              (uint64_t)info.Alignment);
  D3D12_RESOURCE_DESC buffer = {};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = 65536 * 3;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_RESOURCE_ALLOCATION_INFO buffer_info = device->GetResourceAllocationInfo(0, 1, &buffer);
  std::printf("allocation.buffer=%" PRIu64 ",%" PRIu64 "\n", (uint64_t)buffer_info.SizeInBytes,
              (uint64_t)buffer_info.Alignment);
  expect("allocation", info.SizeInBytes >= 1280ull * 720 * 8 && info.Alignment != 0);

  D3D12_HEAP_PROPERTIES default_heap = {};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> resource;
  CHECK(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture,
                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                        IID_PPV_ARGS(&resource)));
  D3D12_RESOURCE_DESC got = resource->GetDesc();
  std::printf("resource.desc=%d,%" PRIu64 ",%" PRIu64 ",%u,%u,%u,%d,%u,%u,%d,%d\n", got.Dimension,
              (uint64_t)got.Alignment, (uint64_t)got.Width, got.Height, got.DepthOrArraySize,
              got.MipLevels, got.Format, got.SampleDesc.Count, got.SampleDesc.Quality, got.Layout,
              got.Flags);
  expect("resource.desc", got.Width == 1280 && got.Height == 720 && got.DepthOrArraySize == 3 &&
                              got.MipLevels == 4 && got.Format == texture.Format &&
                              got.Flags == texture.Flags);
  // The form command_processor.cpp uses: GetDesc() in an expression.
  expect("resource.desc.inline", resource->GetDesc().Width == 1280 &&
                                     resource->GetDesc().Height == 720);

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
  ComPtr<ID3D12CommandQueue> queue;
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  D3D12_COMMAND_QUEUE_DESC got_queue = queue->GetDesc();
  std::printf("queue.desc=%d,%d,%d,%u\n", got_queue.Type, got_queue.Priority, got_queue.Flags,
              got_queue.NodeMask);
  expect("queue.desc", got_queue.Type == D3D12_COMMAND_LIST_TYPE_COPY &&
                           got_queue.Priority == D3D12_COMMAND_QUEUE_PRIORITY_HIGH);

  DXGI_ADAPTER_DESC adapter = {};
  CHECK(warp->GetDesc(&adapter));
  LUID luid = device->GetAdapterLuid();
  std::printf("luid.matches=%d\n", luid.LowPart == adapter.AdapterLuid.LowPart &&
                                       luid.HighPart == adapter.AdapterLuid.HighPart);
  expect("luid", luid.LowPart == adapter.AdapterLuid.LowPart &&
                     luid.HighPart == adapter.AdapterLuid.HighPart);

  std::printf("failures=%d\n", failures);
  return failures ? 2 : 0;
}
