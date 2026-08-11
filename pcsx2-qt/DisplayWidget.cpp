// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "DisplayWidget.h"
#include "MainWindow.h"
#include "QtHost.h"
#include "QtUtils.h"

#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "Input/InputManager.h"

#include "common/Assertions.h"
#include "common/Console.h"

#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QResizeEvent>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtGui/QWindowStateChangeEvent>

#include <bit>
#include <cmath>

#if defined(PCSX2_QT_HAS_WAYLAND_RELATIVE_POINTER)
#include <QtGui/QGuiApplication>
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <QtGui/qguiapplication_platform.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
extern "C" {
#include "wayland-generated-protocols/pointer-constraints-unstable-v1-client-protocol.h"
#include "wayland-generated-protocols/relative-pointer-unstable-v1-client-protocol.h"
}
#endif

#if defined(_WIN32)
#include "common/RedtapeWindows.h"
#endif

DisplaySurface::DisplaySurface()
	: QWindow()
{
	m_resize_debounce_timer = new QTimer(this);
	m_resize_debounce_timer->setSingleShot(true);
	m_resize_debounce_timer->setTimerType(Qt::PreciseTimer);
	connect(m_resize_debounce_timer, &QTimer::timeout, this, &DisplaySurface::onResizeDebounceTimer);
}

DisplaySurface::~DisplaySurface()
{
	destroyWaylandRelativeMouseObjects();
#ifdef _WIN32
	if (m_clip_mouse_enabled)
		ClipCursor(nullptr);
#endif
}

QWidget* DisplaySurface::createWindowContainer(QWidget* parent)
{
	m_container = QWidget::createWindowContainer(this, parent);
	m_container->installEventFilter(this);
	m_container->setFocusPolicy(Qt::StrongFocus);
	return m_container;
}

std::optional<WindowInfo> DisplaySurface::getWindowInfo()
{
	std::optional<WindowInfo> ret(QtUtils::GetWindowInfoForWindow(this));
	if (ret.has_value())
	{
		m_last_window_width = ret->surface_width;
		m_last_window_height = ret->surface_height;
		m_last_window_scale = ret->surface_scale;
	}
	return ret;
}

void DisplaySurface::updateRelativeMode(bool enabled)
{
#ifdef _WIN32
	// prefer ClipCursor() over warping movement when we're using raw input
	bool clip_cursor = enabled && false /*InputManager::IsUsingRawInput()*/;
	if (m_relative_mouse_enabled == enabled && m_clip_mouse_enabled == clip_cursor)
		return;

	DevCon.WriteLn("updateRelativeMode(): relative=%s, clip=%s", enabled ? "yes" : "no", clip_cursor ? "yes" : "no");

	if (!clip_cursor && m_clip_mouse_enabled)
	{
		m_clip_mouse_enabled = false;
		ClipCursor(nullptr);
	}
#else
	if (m_relative_mouse_enabled == enabled)
	{
#if defined(PCSX2_QT_HAS_WAYLAND_RELATIVE_POINTER)
		if (enabled && QGuiApplication::platformName().toLower() == "wayland" && !m_wayland_relative_mouse_enabled)
		{
			updateWaylandRelativeMouseMode();
			if (m_wayland_relative_mouse_enabled)
			{
				m_relative_mouse_last_pos = QCursor::pos();
				setMouseGrabEnabled(true);
			}
		}
#endif
		return;
	}

	DevCon.WriteLn("updateRelativeMode(): relative=%s", enabled ? "yes" : "no");
#endif

	if (enabled)
	{
#ifdef _WIN32
		m_relative_mouse_enabled = !clip_cursor;
		m_clip_mouse_enabled = clip_cursor;
#else
		m_relative_mouse_enabled = true;
#endif
		m_relative_mouse_start_pos = QCursor::pos();
		updateCenterPos();
		m_relative_mouse_last_pos = QCursor::pos();
		setMouseGrabEnabled(true);
	}
	else if (m_relative_mouse_enabled)
	{
		destroyWaylandRelativeMouseObjects();
		m_relative_mouse_enabled = false;
		QCursor::setPos(m_relative_mouse_start_pos);
		setMouseGrabEnabled(false);
	}
}

void DisplaySurface::updateCursor(bool hidden)
{
	if (m_cursor_hidden == hidden)
		return;

	m_cursor_hidden = hidden;
	if (hidden)
	{
		DevCon.WriteLn("updateCursor(): Cursor is now hidden");
		setCursor(Qt::BlankCursor);
	}
	else
	{
		DevCon.WriteLn("updateCursor(): Cursor is now shown");
		unsetCursor();
	}
}

void DisplaySurface::destroyWaylandRelativeMouseObjects()
{
#if defined(PCSX2_QT_HAS_WAYLAND_RELATIVE_POINTER)
	if (m_wayland_relative_pointer)
	{
		zwp_relative_pointer_v1_destroy(static_cast<zwp_relative_pointer_v1*>(m_wayland_relative_pointer));
		m_wayland_relative_pointer = nullptr;
	}
	if (m_wayland_locked_pointer)
	{
		zwp_locked_pointer_v1_destroy(static_cast<zwp_locked_pointer_v1*>(m_wayland_locked_pointer));
		m_wayland_locked_pointer = nullptr;
	}
	if (m_wayland_relative_pointer_manager)
	{
		zwp_relative_pointer_manager_v1_destroy(static_cast<zwp_relative_pointer_manager_v1*>(m_wayland_relative_pointer_manager));
		m_wayland_relative_pointer_manager = nullptr;
	}
	if (m_wayland_pointer_constraints)
	{
		zwp_pointer_constraints_v1_destroy(static_cast<zwp_pointer_constraints_v1*>(m_wayland_pointer_constraints));
		m_wayland_pointer_constraints = nullptr;
	}
	if (m_wayland_registry)
	{
		wl_registry_destroy(static_cast<wl_registry*>(m_wayland_registry));
		m_wayland_registry = nullptr;
	}
#endif
	m_wayland_relative_mouse_enabled = false;
	m_wayland_pointer_serial = 0;
	m_wayland_display = nullptr;
	m_wayland_pointer = nullptr;
	if (!m_cursor_hidden)
	{
		unsetCursor();
		if (m_container)
			m_container->unsetCursor();
	}
}

void DisplaySurface::updateWaylandRelativeMouseMode()
{
#if defined(PCSX2_QT_HAS_WAYLAND_RELATIVE_POINTER)
	if (QGuiApplication::platformName().toLower() != "wayland")
		return;
	if (m_wayland_relative_pointer || m_wayland_locked_pointer)
		return;

	destroyWaylandRelativeMouseObjects();

	QNativeInterface::QWaylandApplication* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
	QPlatformNativeInterface* native_interface = QGuiApplication::platformNativeInterface();
	if (!wayland || !native_interface)
		return;

	wl_display* display = wayland->display();
	wl_pointer* pointer = wayland->pointer();
	m_wayland_display = display;
	m_wayland_pointer = pointer;
	m_wayland_pointer_serial = wayland->lastInputSerial();
	QWindow* constraint_window = this;
	if (m_container && m_container->window() && m_container->window()->windowHandle())
		constraint_window = m_container->window()->windowHandle();
	wl_surface* surface = static_cast<wl_surface*>(native_interface->nativeResourceForWindow("surface", constraint_window));
	if (!surface && constraint_window != this)
	{
		constraint_window = this;
		surface = static_cast<wl_surface*>(native_interface->nativeResourceForWindow("surface", constraint_window));
	}
	if (!display || !pointer || !surface)
		return;

	struct RegistryState
	{
		DisplaySurface* self;
		wl_display* display;
	};

	static constexpr wl_registry_listener registry_listener = {
		[](void* data, wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
			RegistryState* state = static_cast<RegistryState*>(data);
			if (std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0)
			{
				state->self->m_wayland_relative_pointer_manager = wl_registry_bind(
					registry, id, &zwp_relative_pointer_manager_v1_interface, std::min<uint32_t>(version, 1));
			}
			else if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0)
			{
				state->self->m_wayland_pointer_constraints = wl_registry_bind(
					registry, id, &zwp_pointer_constraints_v1_interface, std::min<uint32_t>(version, 1));
			}
		},
		[](void*, wl_registry*, uint32_t) {}};

	m_wayland_registry = wl_display_get_registry(display);
	RegistryState registry_state = {this, display};
	wl_registry_add_listener(static_cast<wl_registry*>(m_wayland_registry), &registry_listener, &registry_state);
	wl_display_roundtrip(display);

	if (!m_wayland_relative_pointer_manager || !m_wayland_pointer_constraints)
	{
		destroyWaylandRelativeMouseObjects();
		return;
	}

	m_wayland_relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
		static_cast<zwp_relative_pointer_manager_v1*>(m_wayland_relative_pointer_manager), pointer);
	m_wayland_locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
		static_cast<zwp_pointer_constraints_v1*>(m_wayland_pointer_constraints), surface, pointer, nullptr,
		ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
	static constexpr zwp_locked_pointer_v1_listener locked_pointer_listener = {
		[](void* data, zwp_locked_pointer_v1*) {
			DisplaySurface* surface = static_cast<DisplaySurface*>(data);
			surface->m_wayland_relative_mouse_enabled = true;
			if (surface->m_wayland_pointer && surface->m_wayland_pointer_serial != 0)
			{
				wl_pointer_set_cursor(static_cast<wl_pointer*>(surface->m_wayland_pointer), surface->m_wayland_pointer_serial, nullptr, 0, 0);
				if (surface->m_wayland_display)
					wl_display_flush(static_cast<wl_display*>(surface->m_wayland_display));
			}
			surface->setCursor(Qt::BlankCursor);
			if (surface->m_container)
				surface->m_container->setCursor(Qt::BlankCursor);
		},
		[](void* data, zwp_locked_pointer_v1*) {
			DisplaySurface* surface = static_cast<DisplaySurface*>(data);
			surface->m_wayland_relative_mouse_enabled = false;
			if (!surface->m_cursor_hidden)
			{
				surface->unsetCursor();
				if (surface->m_container)
					surface->m_container->unsetCursor();
			}
		}};
	zwp_locked_pointer_v1_add_listener(static_cast<zwp_locked_pointer_v1*>(m_wayland_locked_pointer), &locked_pointer_listener, this);
	zwp_locked_pointer_v1_set_cursor_position_hint(static_cast<zwp_locked_pointer_v1*>(m_wayland_locked_pointer),
		wl_fixed_from_int(constraint_window->width() / 2), wl_fixed_from_int(constraint_window->height() / 2));
	wl_surface_commit(surface);
	wl_display_flush(display);

	static constexpr zwp_relative_pointer_v1_listener relative_pointer_listener = {
		[](void*, zwp_relative_pointer_v1*, uint32_t, uint32_t, wl_fixed_t, wl_fixed_t, wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
			const float dx = static_cast<float>(wl_fixed_to_double(dx_unaccel));
			const float dy = static_cast<float>(wl_fixed_to_double(dy_unaccel));
			if (dx != 0.0f)
				InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::X, dx, true);
			if (dy != 0.0f)
				InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::Y, dy, true);
		}};
	zwp_relative_pointer_v1_add_listener(static_cast<zwp_relative_pointer_v1*>(m_wayland_relative_pointer), &relative_pointer_listener, this);
#endif
}

void DisplaySurface::handleCloseEvent(QCloseEvent* event)
{
	// Closing the separate widget will either cancel the close, or trigger shutdown.
	// In the latter case, it's going to destroy us, so don't let Qt do it first.
	// Treat a close event while fullscreen as an exit, that way ALT+F4 closes PCSX2,
	// rather than just the game.
	if (QtHost::IsVMValid() && !isFullScreen())
	{
		QMetaObject::invokeMethod(g_main_window, "requestShutdown", Q_ARG(bool, true),
			Q_ARG(bool, true), Q_ARG(bool, false));
	}
	else
	{
		QMetaObject::invokeMethod(g_main_window, "requestExit", Q_ARG(bool, true));
	}

	// Cancel the event from closing the window.
	event->ignore();
}

bool DisplaySurface::isFullScreen() const
{
	// DisplaySurface may be in a container
	return (parent() ? parent()->windowState() : windowState()) & Qt::WindowFullScreen;
}

void DisplaySurface::setFocus()
{
	if (m_container)
		m_container->setFocus();
	else
		requestActivate();
}

QByteArray DisplaySurface::saveGeometry() const
{
	if (m_container)
		return m_container->saveGeometry();
	else
	{
		// QWindow lacks saveGeometry, so create a dummy widget and copy geometry across.
		QWidget dummy = QWidget();
		dummy.setGeometry(geometry());
		return dummy.saveGeometry();
	}
}

void DisplaySurface::restoreGeometry(const QByteArray& geometry)
{
	if (m_container)
		m_container->restoreGeometry(geometry);
	else
	{
		// QWindow lacks restoreGeometry, so create a dummy widget and copy geometry across.
		QWidget dummy = QWidget();
		dummy.restoreGeometry(geometry);
		setGeometry(dummy.geometry());
	}
}

void DisplaySurface::updateCenterPos()
{
#ifdef _WIN32
	if (m_clip_mouse_enabled)
	{
		RECT rc;
		if (GetWindowRect(reinterpret_cast<HWND>(winId()), &rc))
			ClipCursor(&rc);
	}
	else if (m_relative_mouse_enabled)
	{
		RECT rc;
		if (GetWindowRect(reinterpret_cast<HWND>(winId()), &rc))
		{
			m_relative_mouse_center_pos.setX(((rc.right - rc.left) / 2) + rc.left);
			m_relative_mouse_center_pos.setY(((rc.bottom - rc.top) / 2) + rc.top);
			SetCursorPos(m_relative_mouse_center_pos.x(), m_relative_mouse_center_pos.y());
		}
	}
#else
	if (m_relative_mouse_enabled)
	{
#if defined(PCSX2_QT_HAS_WAYLAND_RELATIVE_POINTER)
		if (QGuiApplication::platformName().toLower() == "wayland")
		{
			if (!m_wayland_relative_mouse_enabled)
				updateWaylandRelativeMouseMode();
			m_relative_mouse_last_pos = QCursor::pos();
			m_relative_mouse_ignore_warp_event = false;
			m_relative_mouse_has_pending_warp_delta = false;
			return;
		}
#endif

		// we do a round trip here because these coordinates are dpi-unscaled
		m_relative_mouse_center_pos = mapToGlobal(QPoint((width() + 1) / 2, (height() + 1) / 2));
		QCursor::setPos(m_relative_mouse_center_pos);
		m_relative_mouse_center_pos = QCursor::pos();
		m_relative_mouse_last_pos = m_relative_mouse_center_pos;
		m_relative_mouse_ignore_warp_event = true;
		m_relative_mouse_has_pending_warp_delta = false;
	}
#endif
}

// Keyboard focus and child windows are inconsistant across platforms;
// Windows: Can programmatically focus the child window, NVidia overlay can defocus it.
// X11: Can programmatically focus the child window.
// Wayland: Child window cannot be focused at all on most(?) DE.
// Mac: Can programmatically focus the child window.
// Thus for KB inputs we need to sometimes use the event filter.
// Mouse events are always delivered to the child window, so that seems consistant.
void DisplaySurface::handleKeyInputEvent(QEvent* event)
{
	switch (event->type())
	{
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		{
			const QKeyEvent* key_event = static_cast<QKeyEvent*>(event);

			// Forward text input to imgui.
			if (ImGuiManager::WantsTextInput() && key_event->type() == QEvent::KeyPress)
			{
				// Don't forward backspace characters. We send the backspace as a normal key event,
				// so if we send the character too, it double-deletes.
				QString text(key_event->text());
				text.remove(QChar('\b'));
				if (!text.isEmpty())
					ImGuiManager::AddTextInput(text.toStdString());
			}

			if (key_event->isAutoRepeat())
				return;

			// For some reason, Windows sends "fake" key events.
			// Scenario: Press shift, press F1, release shift, release F1.
			// Events: Shift=Pressed, F1=Pressed, Shift=Released, **F1=Pressed**, F1=Released.
			// To work around this, we keep track of keys pressed with modifiers in a list, and
			// discard the press event when it's been previously activated. It's pretty gross,
			// but I can't think of a better way of handling it, and there doesn't appear to be
			// any window flag which changes this behavior that I can see.

			const u32 key = QtUtils::KeyEventToCode(key_event);
			const Qt::KeyboardModifiers modifiers = key_event->modifiers();
			const bool pressed = (key_event->type() == QEvent::KeyPress);
			const auto it = std::find(m_keys_pressed_with_modifiers.begin(), m_keys_pressed_with_modifiers.end(), key);
			if (it != m_keys_pressed_with_modifiers.end())
			{
				if (pressed)
					return;
				else
					m_keys_pressed_with_modifiers.erase(it);
			}
			else if (modifiers != Qt::NoModifier && modifiers != Qt::KeypadModifier && pressed)
			{
				m_keys_pressed_with_modifiers.push_back(key);
			}

			Host::RunOnCPUThread([key, pressed]() {
				InputManager::InvokeEvents(InputManager::MakeHostKeyboardKey(key), static_cast<float>(pressed));
			});

			return;
		}

		default:
			pxAssert(false);
			return;
	}
}

void DisplaySurface::onResizeDebounceTimer()
{
	emit windowResizedEvent(m_last_window_width, m_last_window_height, m_last_window_scale);
}

bool DisplaySurface::event(QEvent* event)
{
	switch (event->type())
	{
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		{
			handleKeyInputEvent(event);
			return true;
		}

		case QEvent::MouseMove:
		{
			const QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);

			if (!m_relative_mouse_enabled)
			{
				const qreal dpr = devicePixelRatio();
				const QPoint mouse_pos = mouse_event->pos();

				const float scaled_x = static_cast<float>(static_cast<qreal>(mouse_pos.x()) * dpr);
				const float scaled_y = static_cast<float>(static_cast<qreal>(mouse_pos.y()) * dpr);
				InputManager::UpdatePointerAbsolutePosition(0, scaled_x, scaled_y);
			}
			else
			{
				if (m_wayland_relative_mouse_enabled)
					return true;

				const QPoint mouse_pos = mouse_event->globalPosition().toPoint();
				const QPoint event_delta = mouse_pos - m_relative_mouse_last_pos;
				if (mouse_event->source() == Qt::MouseEventSynthesizedByApplication ||
					(m_relative_mouse_has_pending_warp_delta && event_delta == m_relative_mouse_pending_warp_delta) ||
					(mouse_pos == m_relative_mouse_center_pos &&
						(m_relative_mouse_ignore_warp_event || mouse_pos != m_relative_mouse_last_pos)))
				{
					m_relative_mouse_last_pos = mouse_pos;
					m_relative_mouse_ignore_warp_event = false;
					m_relative_mouse_has_pending_warp_delta = false;
					return true;
				}
				m_relative_mouse_ignore_warp_event = false;
				m_relative_mouse_has_pending_warp_delta = false;

				// On windows, we use winapi here. The reason being that the coordinates in QCursor
				// are un-dpi-scaled, so we lose precision at higher desktop scalings.
				float dx = 0.0f, dy = 0.0f;

#ifndef _WIN32
				if (mouse_pos != m_relative_mouse_last_pos)
				{
					dx = static_cast<float>(event_delta.x());
					dy = static_cast<float>(event_delta.y());
					QCursor::setPos(m_relative_mouse_center_pos);
					const QPoint warped_pos = QCursor::pos();
					if (warped_pos == m_relative_mouse_center_pos)
					{
						m_relative_mouse_last_pos = warped_pos;
						m_relative_mouse_ignore_warp_event = true;
					}
					else
					{
						m_relative_mouse_last_pos = mouse_pos;
						m_relative_mouse_pending_warp_delta = m_relative_mouse_center_pos - mouse_pos;
						m_relative_mouse_has_pending_warp_delta = true;
					}
				}
#else
				POINT native_mouse_pos;
				if (GetCursorPos(&native_mouse_pos))
				{
					dx = static_cast<float>(native_mouse_pos.x - m_relative_mouse_last_pos.x());
					dy = static_cast<float>(native_mouse_pos.y - m_relative_mouse_last_pos.y());
					SetCursorPos(m_relative_mouse_center_pos.x(), m_relative_mouse_center_pos.y());
					POINT warped_pos;
					if (GetCursorPos(&warped_pos) && warped_pos.x == m_relative_mouse_center_pos.x() && warped_pos.y == m_relative_mouse_center_pos.y())
					{
						m_relative_mouse_last_pos = m_relative_mouse_center_pos;
						m_relative_mouse_ignore_warp_event = true;
					}
					else
					{
						m_relative_mouse_last_pos = QPoint(native_mouse_pos.x, native_mouse_pos.y);
					}
				}
#endif

				if (dx != 0.0f)
					InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::X, dx, true);
				if (dy != 0.0f)
					InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::Y, dy, true);
			}

			return true;
		}

		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseButtonRelease:
		{
			if (const u32 button_mask = static_cast<u32>(static_cast<const QMouseEvent*>(event)->button()))
			{
				Host::RunOnCPUThread([button_index = std::countr_zero(button_mask),
									 pressed = (event->type() != QEvent::MouseButtonRelease)]() {
					InputManager::InvokeEvents(
						InputManager::MakePointerButtonKey(0, button_index), static_cast<float>(pressed));
				});
			}

			// don't toggle fullscreen when we're bound.. that wouldn't end well.
			if (event->type() == QEvent::MouseButtonDblClick &&
				static_cast<const QMouseEvent*>(event)->button() == Qt::LeftButton &&
				QtHost::IsVMValid() && !FullscreenUI::HasActiveWindow() &&
				((!QtHost::IsVMPaused() && !InputManager::HasAnyBindingsForKey(InputManager::MakePointerButtonKey(0, 0))) ||
					(QtHost::IsVMPaused() && !ImGuiManager::WantsMouseInput())) &&
				Host::GetBoolSettingValue("UI", "DoubleClickTogglesFullscreen", true))
			{
				g_emu_thread->toggleFullscreen();
			}

			return true;
		}

		case QEvent::Wheel:
		{
			const QPoint delta_angle(static_cast<QWheelEvent*>(event)->angleDelta());
			const float dx = std::clamp(static_cast<float>(delta_angle.x()) / QtUtils::MOUSE_WHEEL_DELTA, -1.0f, 1.0f);
			if (dx != 0.0f)
				InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::WheelX, dx);

			const float dy = std::clamp(static_cast<float>(delta_angle.y()) / QtUtils::MOUSE_WHEEL_DELTA, -1.0f, 1.0f);
			if (dy != 0.0f)
				InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::WheelY, dy);

			return true;
		}

		case QEvent::DevicePixelRatioChange:
		case QEvent::Resize:
		{
			QWindow::event(event);

			const float dpr = devicePixelRatio();
			const u32 scaled_width = static_cast<u32>(std::max(static_cast<int>(std::round(static_cast<qreal>(width()) * dpr)), 1));
			const u32 scaled_height = static_cast<u32>(std::max(static_cast<int>(std::round(static_cast<qreal>(height()) * dpr)), 1));

			// avoid spamming resize events for paint events (sent on move on windows)
			if (m_last_window_width != scaled_width || m_last_window_height != scaled_height || m_last_window_scale != dpr)
			{
				m_last_window_width = scaled_width;
				m_last_window_height = scaled_height;
				m_last_window_scale = dpr;
				// qt spams resize events, sometimes several time per ms.
				// since a vulkan resize swap chain event takes between 15 to 25ms this is,
				// need less to say, unwanted.
				m_resize_debounce_timer->start(100);
			}

			updateCenterPos();
			return true;
		}

		case QEvent::DragEnter:
			QWindow::event(event);
			emit dragEnterEvent(static_cast<QDragEnterEvent*>(event));
			return event->isAccepted();

		case QEvent::Drop:
			QWindow::event(event);
			emit dropEvent(static_cast<QDropEvent*>(event));
			return event->isAccepted();

		case QEvent::Move:
			updateCenterPos();
			return true;

		// These events only work on the top level control.
		// Which is this container when render to seperate or fullscreen is active (Windows).
		case QEvent::Close:
			handleCloseEvent(static_cast<QCloseEvent*>(event));
			return true;
		case QEvent::WindowStateChange:
			if (static_cast<QWindowStateChangeEvent*>(event)->oldState() & Qt::WindowMinimized)
				emit windowRestoredEvent();
			return false;

		default:
			return QWindow::event(event);
	}
}

bool DisplaySurface::eventFilter(QObject* object, QEvent* event)
{
	switch (event->type())
	{
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
#ifdef _WIN32
			// Nvidia overlay causes the child window to lose focus, but not its parent.
			// Refocus the child window.
			requestActivate();
#endif
			handleKeyInputEvent(event);
			return true;

		// These events only work on the top level control.
		// Which is this container when render to seperate or fullscreen is active (Non-Windows).
		case QEvent::Close:
			handleCloseEvent(static_cast<QCloseEvent*>(event));
			return true;
		case QEvent::WindowStateChange:
			if (static_cast<QWindowStateChangeEvent*>(event)->oldState() & Qt::WindowMinimized)
				emit windowRestoredEvent();
			return false;

		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseButtonRelease:
		{
			if (const u32 button_mask = static_cast<u32>(static_cast<const QMouseEvent*>(event)->button()))
			{
				Host::RunOnCPUThread([button_index = std::countr_zero(button_mask),
									 pressed = (event->type() != QEvent::MouseButtonRelease)]() {
					InputManager::InvokeEvents(
						InputManager::MakePointerButtonKey(0, button_index), static_cast<float>(pressed));
				});
			}

			return true;
		}

		case QEvent::ChildWindowRemoved:
			if (static_cast<QChildWindowEvent*>(event)->child() == this)
			{
				object->removeEventFilter(this);
				m_container = nullptr;
			}
			return false;

		case QEvent::FocusIn:
			// macOS: When we (the display window) get focus from another window with a toolbar we update to the MainWindow toolbar.
			// This is because we are a different native window from our MainWindow. So, whenever we get focus, focus our MainWindow.
			// That way macOS will show the MainWindow toolbar when you click from the debugger / log window to the game.

			// Don't try to steal focus when we're showing a modal dialog
			// We end up ping ponging focus in a feedback loop
			if (QApplication::activeModalWidget() != nullptr)
				return false;

			if (const auto* w = qobject_cast<QWidget*>(object))
				w->window()->activateWindow();

			return false;
		default:
			return false;
	}
}

#include "moc_DisplayWidget.cpp"
