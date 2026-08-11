// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include "common/WindowInfo.h"
#include <QtCore/QTimer>
#include <QtGui/QDragMoveEvent>
#include <QtGui/QWindow>
#include <optional>
#include <vector>

class QCloseEvent;

class DisplaySurface final : public QWindow
{
	Q_OBJECT

public:
	explicit DisplaySurface();
	~DisplaySurface();

	// while QWindow can be used directly as a window, Popups requre a QWidget parent.
	// Additionally, we use saveGeometry/restoreGeometry for render to seperate window mode
	// but those functions only exist on QWidget.
	// Thus, we always need a container widget.
	QWidget* createWindowContainer(QWidget* parent = nullptr);

	std::optional<WindowInfo> getWindowInfo();

	void updateRelativeMode(bool enabled);
	void updateCursor(bool hidden);

	bool isFullScreen() const;
	void setFocus();

	QByteArray saveGeometry() const;
	void restoreGeometry(const QByteArray& geometry);

Q_SIGNALS:
	void windowResizedEvent(u32 width, u32 height, float scale);
	void windowRestoredEvent();

	void dragEnterEvent(QDragEnterEvent* event);
	void dropEvent(QDropEvent* event);

protected:
	void handleCloseEvent(QCloseEvent* event);
	void handleKeyInputEvent(QEvent* event);
	bool event(QEvent* event) override;
	bool eventFilter(QObject* object, QEvent* event) override;

private Q_SLOTS:
	void onResizeDebounceTimer();

private:
	void updateCenterPos();
	void updateWaylandRelativeMouseMode();
	void destroyWaylandRelativeMouseObjects();

	QPoint m_relative_mouse_start_pos{};
	QPoint m_relative_mouse_center_pos{};
	QPoint m_relative_mouse_last_pos{};
	QPoint m_relative_mouse_pending_warp_delta{};
	bool m_relative_mouse_ignore_warp_event = false;
	bool m_relative_mouse_has_pending_warp_delta = false;
	bool m_relative_mouse_enabled = false;
	bool m_wayland_relative_mouse_enabled = false;
	uint32_t m_wayland_pointer_serial = 0;
	void* m_wayland_display = nullptr;
	void* m_wayland_pointer = nullptr;
	void* m_wayland_registry = nullptr;
	void* m_wayland_relative_pointer_manager = nullptr;
	void* m_wayland_pointer_constraints = nullptr;
	void* m_wayland_relative_pointer = nullptr;
	void* m_wayland_locked_pointer = nullptr;
#ifdef _WIN32
	bool m_clip_mouse_enabled = false;
#endif
	bool m_cursor_hidden = false;

	std::vector<int> m_keys_pressed_with_modifiers;

	u32 m_last_window_width = 0;
	u32 m_last_window_height = 0;
	float m_last_window_scale = 1.0f;

	QTimer* m_resize_debounce_timer = nullptr;
	QWidget* m_container = nullptr;
};
