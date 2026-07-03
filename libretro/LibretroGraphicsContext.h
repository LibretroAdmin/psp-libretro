#pragma once
#include <atomic>

#include <libretro.h>
#include "Common/GraphicsContext.h"
#include "Common/GPU/thin3d_create.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "GPU/Software/SoftGpu.h"
#include "headless/Compare.h"
#include "Common/Data/Convert/ColorConv.h"

#define NATIVEWIDTH  480
#define NATIVEHEIGHT 272

class LibretroGraphicsContext : public GraphicsContext {
public:
	LibretroGraphicsContext() {}
	~LibretroGraphicsContext() override { Shutdown(); }

	virtual bool Init() = 0;
	virtual void SetRenderTarget() {}
	virtual GPUCore GetGPUCore() = 0;
	virtual const char *Ident() = 0;

	void Shutdown() override {
		DestroyDrawContext();
	}
	virtual void SwapBuffers() = 0;
	void Resize() override {}

	virtual void GotBackbuffer();
	virtual void LostBackbuffer();

	virtual void CreateDrawContext() {}
	virtual void DestroyDrawContext() {
		if (!draw_) {
			return;
		}
		delete draw_;
		draw_ = nullptr;
	}
	Draw::DrawContext *GetDrawContext() override { return draw_; }

	static LibretroGraphicsContext *CreateGraphicsContext();

	static retro_video_refresh_t video_cb;

protected:
	Draw::DrawContext *draw_ = nullptr;
};

class LibretroHWRenderContext : public LibretroGraphicsContext {
public:
	LibretroHWRenderContext(retro_hw_context_type context_type, unsigned version_major = 0, unsigned version_minor = 0);
	bool Init(bool cache_context);
	void SetRenderTarget() override {}
	void SwapBuffers() override {
		video_cb(RETRO_HW_FRAME_BUFFER_VALID, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0);
	}
	virtual void ContextReset();
	virtual void ContextDestroy();

protected:
	retro_hw_render_callback hw_render_ = {};
};

class LibretroSoftwareContext : public LibretroGraphicsContext {
public:
	LibretroSoftwareContext() {}
	bool Init() override { return true; }
	void SwapBuffers() override {
		u32 w = NATIVEWIDTH;
		u32 h = NATIVEHEIGHT;
		if (!gpu) {
			// Nothing has been rendered yet. Tell the frontend to
			// duplicate the previous frame instead of scanning out
			// uninitialized data.
			video_cb(NULL, w, h, w * sizeof(u32));
			return;
		}
		GPUDebugBuffer buf;
		gpu->GetOutputFramebuffer(buf);
		// TranslateDebugBufferToCompare already produces a tightly packed
		// XRGB8888 buffer we can hand to the frontend directly; video_cb is
		// synchronous, so the vector's lifetime is sufficient.
		const std::vector<u32> pixels = TranslateDebugBufferToCompare(&buf, w, h);
		if (pixels.size() < (size_t)w * h) {
			// Unknown/unsupported debug buffer format - conversion bailed.
			// (The previous code memcpy'd a full frame from the empty
			// vector here: a heap out-of-bounds read.)
			video_cb(NULL, w, h, w * sizeof(u32));
			return;
		}
		const u32 *data = pixels.data();
		if (g_Config.bDisplayCropTo16x9) {
			// Crop 480x272 to 480x270 (16:9): skip the first row, drop the last.
			data += w;
			h -= 2;
		}
		video_cb(data, w, h, w * sizeof(u32));
	}
	GPUCore GetGPUCore() override { return GPUCORE_SOFTWARE; }
	const char *Ident() override { return "Software"; }
};

namespace Libretro {
extern LibretroGraphicsContext *ctx;
extern retro_environment_t environ_cb;
extern retro_hw_context_type backend;

enum class EmuThreadState {
	DISABLED,
	START_REQUESTED,
	RUNNING,
	PAUSE_REQUESTED,
	PAUSED,
	QUIT_REQUESTED,
	STOPPED,
};
extern bool useEmuThread;
extern std::atomic<EmuThreadState> emuThreadState;
void EmuThreadStart();
void EmuThreadStop();
void EmuThreadPause();
} // namespace Libretro
