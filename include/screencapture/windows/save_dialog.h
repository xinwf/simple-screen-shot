#pragma once
#include "screencapture/ui/base_window.h"

namespace sc::windows {

// gnome-screenshot style post-capture dialog, shown after the toolbar's
// confirm button: image preview, editable name, target folder and
// Cancel / Copy-to-clipboard / Save actions. Owns the final app exit when
// running with --auto-quit (the capture windows close before it opens).
class SaveDialogWindow : public sc::ui::BaseWindow {
public:
    static void open(std::shared_ptr<gfx::ImageSurface> image, bool quitWhenDone);
    static void dispose();
    static bool hasWindow() { return instance_ != nullptr; }

    void onCreated() override;
    void onKeyDown(uint32_t key) override;
    void onTextInput(const std::string& utf8) override;
    void onMouseDown(const core::PointF& pos, bool right) override;
    void onMouseMove(const core::PointF& pos) override;
    void onMouseUp(const core::PointF& pos, bool right) override;

private:
    void finish(bool saved);
    std::filesystem::path targetPath() const;
    void updateControls();
    sc::ui::TextBox* focusedField() const;   // whichever TextBox has focus
    void focusField(sc::ui::TextBox* box);   // focus one field, blur the other

    static std::unique_ptr<SaveDialogWindow> instance_;

    std::shared_ptr<gfx::ImageSurface> image_;
    bool quitWhenDone_{false};
    std::filesystem::path dir_;
    float previewW_{340.f};   // follows the image aspect so the shot fills it
    float previewH_{190.f};
    sc::ui::TextBox* nameBox_{nullptr};
    sc::ui::WidgetNode* nameField_{nullptr};
    sc::ui::TextBox* dirBox_{nullptr};       // editable target directory
    sc::ui::WidgetNode* dirField_{nullptr};
    sc::ui::TextBox* pressBox_{nullptr};     // field the mouse selection started in
    bool fieldPressing_{false};              // mouse held inside a text field
    long long lastClickMs_{0};               // double-click detection
    float lastClickX_{0.f}, lastClickY_{0.f};
    sc::ui::Label* dirLabel_{nullptr};
    sc::ui::CanvasNode* preview_{nullptr};
    bool dragging_{false};
    float absPressX_{0.f}, absPressY_{0.f};   // absolute pointer pos at drag start
    int winX0_{0}, winY0_{0};
};

} // namespace sc::windows
