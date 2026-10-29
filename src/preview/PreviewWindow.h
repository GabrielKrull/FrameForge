#pragma once

#include <windows.h>

#include <functional>

namespace frameforge {

// ============================================================
// Thin Win32 preview window.
//
// Owns the window handle and pumps its message queue. Rendering
// is handled separately by PreviewController so this class stays
// free of graphics concerns.
// ============================================================
class PreviewWindow {
public:
    using ResizeCallback = std::function<void(UINT, UINT)>;

    PreviewWindow() = default;
    ~PreviewWindow();

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    // Create the window. Must be called from the thread that will
    // pump messages (normally the main thread).
    HRESULT Create(const wchar_t* title, int clientWidth, int clientHeight);

    // Notified whenever the client area changes (WM_SIZE).
    void SetResizeCallback(ResizeCallback callback);

    // Update the window caption (UI thread only). No-op when the
    // window is not open.
    void SetTitle(const wchar_t* title);

    // Pump pending messages. Returns false when WM_QUIT was received
    // or the window no longer exists.
    bool PumpMessages();

    void Close();

    HWND Handle() const { return m_hwnd; }
    bool IsOpen() const { return m_hwnd != nullptr; }

    // Window procedure (public so it can be installed via
    // RegisterClassExW by the helper in the implementation file).
    static LRESULT CALLBACK WndProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hwnd = nullptr;
    ResizeCallback m_resizeCallback;
};

} // namespace frameforge
