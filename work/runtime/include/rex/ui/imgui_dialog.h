/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#ifndef REX_UI_IMGUI_DIALOG_H_
#define REX_UI_IMGUI_DIALOG_H_

#include <memory>
#include <vector>

#include <rex/thread.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window_listener.h>

struct ImGuiIO;

namespace rex {
namespace ui {

class ImGuiDialog {
 public:
  virtual ~ImGuiDialog();

  // Shows a simple message box containing a text message.
  // Callers can want for the dialog to close with Wait().
  // Dialogs retain themselves and will delete themselves when closed.
  static ImGuiDialog* ShowMessageBox(ImGuiDrawer* imgui_drawer, std::string title,
                                     std::string body);

  // A fence to signal when the dialog is closed.
  void Then(rex::thread::Fence* fence);

  void Draw();

 protected:
  ImGuiDialog(ImGuiDrawer* imgui_drawer);

  ImGuiDrawer* imgui_drawer() const { return imgui_drawer_; }
  ImGuiIO& GetIO();

  // Closes the dialog and returns to any waiters.
  void Close();

  virtual void OnShow() {}
  virtual void OnClose() {}
  virtual void OnDraw(ImGuiIO& io) { (void)io; }

 public:
  // An idle dialog shows nothing that changes on its own: a toast with nothing
  // queued, or an overlay whose text follows the guest frames. While every
  // dialog is idle, the drawer is painted only with the guest output's frames
  // instead of at every UI tick (the monitor's refresh), so the host presents
  // as often as the guest renders (docs/analysis/d3d-hle-n4.md 17.22). Called
  // on the UI thread.
  virtual bool IsIdle() const { return false; }
  // An empty dialog draws nothing at all, such as a toast with nothing queued.
  // While every dialog is empty, the drawer leaves the presenter, which then
  // presents the guest output from the thread that produced it, as without a
  // user interface; the dialog calls ImGuiDrawer::WakeFromAnyThread when it
  // gets something to draw. Called on the UI thread.
  virtual bool IsEmpty() const { return false; }

 protected:

 private:
  ImGuiDrawer* imgui_drawer_ = nullptr;
  bool has_close_pending_ = false;
  std::vector<rex::thread::Fence*> waiting_fences_;
};

}  // namespace ui
}  // namespace rex

#endif  // REX_UI_IMGUI_DIALOG_H_
