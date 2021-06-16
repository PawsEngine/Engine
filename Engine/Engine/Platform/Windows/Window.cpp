#include "Engine/Platform/Windows/Window.h"
#include "Engine/Platform/Windows/UnicodeHelper.h"
#include <ShellScalingApi.h>
#include "Engine/Application/Engine.h"
#include "Engine/System/Thread/JobSystem.h"

namespace Engine::PlatformSpecific::Windows {
	typename WindowManager::_Initializer WindowManager::_initializer{};

	WindowManager::_Initializer::_Initializer() {
		// Make console support UTF-8
		SetConsoleOutputCP(65001);

		SetProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE);

		// Register basic window class
		WNDCLASSW wc = {};
		wc.lpszClassName = Window::GlobalWindowClassName;
		wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
		wc.cbClsExtra = NULL;
		wc.cbWndExtra = NULL;
		wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
		wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
		wc.hbrBackground = (HBRUSH)(GetStockObject(GRAY_BRUSH));
		wc.lpszMenuName = NULL;
		wc.hInstance = NULL;
		wc.lpfnWndProc = Window::WndProc;

		bool succeeded = RegisterClassW(&wc);
		FATAL_ASSERT(succeeded, u8"RegisterClassW failed to register window class!");
	}

	WindowManager::_Initializer::~_Initializer() {
		UnregisterClassW(Window::GlobalWindowClassName, NULL);
	}

	void WindowManager::Update() {
		auto func = [](Job* job) {
			MSG msg = {};
			if (PeekMessageW(&msg, NULL, NULL, NULL, PM_REMOVE)) {
				if (msg.message != WM_QUIT) {
					TranslateMessage(&msg);
					DispatchMessageW(&msg);
				}
			}
		};
		ENGINEINST->GetJobSystem()->AddJob(func, nullptr, 0, Job::Preference::Window);
	}

	Window* Window::GetFromHWnd(HWND hWnd) {
		if (!IsWindow(hWnd)) {
			return nullptr;
		}

		LONG_PTR p = GetWindowLongPtrW(hWnd, GWLP_USERDATA);
		if (p == NULL) {
			return nullptr;
		}

		Window* wp = (Window*)p;

		return wp;
	}




	struct _NWWInit {
		LONG_PTR userDataPtr;
		HWND result;
	};
	struct _NWWDestroy {
		HWND hWnd;
	};
	struct _NWWGetTitle {
		HWND hWnd;
		UniquePtr<WCHAR[]>* buffer;
	};
	struct _NWWSetTitle {
		HWND hWnd;
		WCHAR* title;
		bool result;
	};
	struct _NWWGetVector2 {
		HWND hWnd;
		int32 x;
		int32 y;
	};
	struct _NWWSetVector2 {
		HWND hWnd;
		int32 x;
		int32 y;
		bool result;
	};
	struct _NWWHasStyleFlag {
		HWND hWnd;
		bool result;
	};
	struct _NWWSetStyleFlag {
		HWND hWnd;
		bool enabled;
		bool result;
	};


	Window::Window() {
		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWInit>();

			HWND w = CreateWindowExW(DefaultWindowExStyle,GlobalWindowClassName, L"", DefaultWindowStyle, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);
			if (!IsWindow(w)) {
				data->result = NULL;
				return;
			}
			// Set user data to let hwnd trace back to Window.
			SetWindowLongPtrW(w, GWLP_USERDATA, data->userDataPtr);

			data->result = w;
		};
		
		// Job data
		_NWWInit data = {};
		data.userDataPtr = (LONG_PTR)this;
		// Start job
		auto js = ENGINEINST->GetJobSystem();
		auto job=js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		// Wait for the result
		js->WaitJob(job);

		HWND w = job->GetDataAs<_NWWInit>()->result;
		ERR_ASSERT(IsWindow(w), u8"CreateWindowW failed to create a window!", return);
		hWnd = w;
	}

	Window::~Window() {
		if (!IsWindow(hWnd)) {
			return;
		}

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWDestroy>();
			DestroyWindow(data->hWnd);
		};
		_NWWDestroy data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
	}

	LRESULT CALLBACK Window::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
		switch (message) {
			case WM_CLOSE:
			{
				Window* nw = Window::GetFromHWnd(hWnd);
				if (nw != nullptr) {
					nw->GetManager()->Destroy(nw->GetId());
				} else {
					ERR_MSG(u8"User data in hWnd is not a Window ptr! This shouldn't happen!");
					DestroyWindow(hWnd);
				}
			}
				return 0;

			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;

			case WM_KEYDOWN:
			{
				Window* nw = Window::GetFromHWnd(hWnd);
				if (nw != nullptr) {
					Variant key = wParam;
					const Variant* args[1] = { &key };
					nw->EmitSignal(STRL("KeyDown"), (const Variant**)args, 1);
				}
				break;
			}
		}
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

	bool Window::IsValid() const {
		return IsWindow(hWnd);
	}
	
	String Window::GetTitle() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return String::GetEmpty());

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWGetTitle>();
			HWND hWnd = data->hWnd;
			int len = GetWindowTextLengthW(hWnd);
			if (len <= 0) {
				return;
			}
			len += 1;

			UniquePtr<WCHAR[]> buffer = UniquePtr<WCHAR[]>::Create(len);
			GetWindowTextW(hWnd, buffer.GetRaw(), len);

			*(data->buffer) = Memory::Move(buffer);
		};

		UniquePtr<WCHAR[]> buffer;

		_NWWGetTitle data = {};
		data.hWnd = hWnd;
		data.buffer = &buffer;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		if (buffer == nullptr) {
			return String::GetEmpty();
		}
		String result;
		bool succeeded = UnicodeHelper::UnicodeToUTF8(buffer.GetRaw(), result);
		ERR_ASSERT(succeeded, u8"Failed to convert Windows wide string to engine string!", return String::GetEmpty());

		return result;
	}
	bool Window::SetTitle(const String& title) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		UniquePtr<WCHAR[]> buffer;
		bool succeeded = UnicodeHelper::UTF8ToUnicode(title, buffer);
		
		ERR_ASSERT(succeeded, u8"Failed to convert engine string to Windows wide string!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetTitle>();
			bool succeeded = SetWindowTextW(data->hWnd, data->title);
			data->result = succeeded;
		};

		_NWWSetTitle data = {};
		data.hWnd = hWnd;
		data.title = buffer.GetRaw();

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		succeeded = job->GetDataAs<_NWWSetTitle>()->result;
		ERR_ASSERT(succeeded, u8"SetWindowTextW failed to set window title!", return false);
		
		return true;
	}

	Vector2 Window::GetPosition() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return Vector2());

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWGetVector2>();

			POINT pos{};
			ClientToScreen(data->hWnd, &pos);

			data->x = pos.x;
			data->y = pos.y;
		};

		_NWWGetVector2 data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		auto rdata = job->GetDataAs<_NWWGetVector2>();

		return Vector2(rdata->x, rdata->y);
	}
	bool Window::SetPosition(const Vector2& position) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		RECT rect = {};
		rect.left = (int)position.x;
		rect.top = (int)position.y;
		rect.right = 100;
		rect.bottom = 100;
		bool succeeded = AdjustWindowRectEx(&rect, GetStyle(), FALSE,GetExStyle());
		ERR_ASSERT(succeeded, u8"AdjustWindowRect failed to calculate window rect!", return false);

		// Job
		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetVector2>();
			bool succeeded = SetWindowPos(data->hWnd, NULL, data->x, data->y, 0, 0, SWP_NOREPOSITION | SWP_NOSIZE);
			data->result = succeeded;
		};

		_NWWSetVector2 data = {};
		data.hWnd = hWnd;
		data.x = rect.left;
		data.y = rect.top;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		succeeded = job->GetDataAs<_NWWSetVector2>()->result;
		ERR_ASSERT(succeeded, u8"SetWindowPos failed to set window rect!", return false);
		return true;
	}
	
	Vector2 Window::GetSize() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return Vector2());

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWGetVector2>();
			
			RECT rect = {};
			GetClientRect(data->hWnd, &rect);

			data->x = rect.right - rect.left;
			data->y = rect.bottom - rect.top;
		};
		
		_NWWGetVector2 data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		auto rdata = job->GetDataAs<_NWWGetVector2>();

		return Vector2(rdata->x, rdata->y);
	}
	bool Window::SetSize(const Vector2& size) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		RECT rect = {};
		rect.left = 0;
		rect.top = 0;
		rect.right = (int)size.x;
		rect.bottom = (int)size.y;
		AdjustWindowRectEx(&rect, GetStyle(), FALSE,GetExStyle());

		// Job
		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetVector2>();
			bool succeeded = SetWindowPos(data->hWnd, NULL, 0, 0, data->x, data->y, SWP_NOREPOSITION | SWP_NOMOVE);
			data->result = succeeded;
		};

		_NWWSetVector2 data = {};
		data.hWnd = hWnd;
		data.x = rect.right - rect.left;
		data.y = rect.bottom - rect.top;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		bool succeeded = job->GetDataAs<_NWWSetVector2>()->result;
		ERR_ASSERT(succeeded, u8"SetWindowPos failed to set window rect!", return false);
		return true;
	}

	bool Window::IsVisible() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = Window::HasStyleFlag(data->hWnd, WS_VISIBLE);
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetVisible(bool visible) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);
		
		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			ShowWindow(data->hWnd, data->enabled ? SW_SHOW : SW_HIDE);
			//UpdateWindow(data->hWnd);
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = visible;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		return true;
	}
	
	bool Window::IsMinimized() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = Window::HasStyleFlag(data->hWnd, WS_MINIMIZE);
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetMinimized(bool minimized) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			ShowWindow(data->hWnd, data->enabled ? SW_MINIMIZE : SW_RESTORE);
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = minimized;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		return true;
	}
	bool Window::IsMaximized() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = Window::HasStyleFlag(data->hWnd, WS_MAXIMIZE);
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetMaximized(bool maximized) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			ShowWindow(data->hWnd, data->enabled ? SW_MAXIMIZE : SW_RESTORE);
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = maximized;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		return true;
	}
	bool Window::HasCloseButton() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = GetMenuState(GetSystemMenu(data->hWnd, false), SC_CLOSE, MF_BYCOMMAND) & MF_ENABLED;
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetCloseButton(bool enabled) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			DWORD flag = (data->enabled ? MF_ENABLED : MF_DISABLED | MF_GRAYED);
			bool succeeded = EnableMenuItem(GetSystemMenu(data->hWnd, false), SC_CLOSE, MF_BYCOMMAND | flag);
			data->result = succeeded;
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = enabled;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		bool succeeded = job->GetDataAs<_NWWSetStyleFlag>()->result;
		return succeeded;
	}
	bool Window::HasMinimizeButton() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = Window::HasStyleFlag(data->hWnd, WS_MINIMIZEBOX);
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetMinimizeButton(bool enabled) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			bool succeeded = Window::SetStyleFlag(data->hWnd, WS_MINIMIZEBOX, data->enabled);
			data->result = succeeded;
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = enabled;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		bool succeeded = job->GetDataAs<_NWWSetStyleFlag>()->result;
		return succeeded;
	}
	bool Window::HasMaximizeButton() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = Window::HasStyleFlag(data->hWnd, WS_MAXIMIZEBOX);
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetMaximizeButton(bool enabled) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			bool succeeded = Window::SetStyleFlag(data->hWnd, WS_MAXIMIZEBOX, data->enabled);
			data->result = succeeded;
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = enabled;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		bool succeeded = job->GetDataAs<_NWWSetStyleFlag>()->result;
		return succeeded;
	}

	bool Window::HasBorder() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = Window::HasStyleFlag(data->hWnd, WS_BORDER);
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetBorder(bool enabled) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			HWND hWnd = data->hWnd;

			POINT pos{};
			ClientToScreen(hWnd, &pos);
			RECT rect = {};
			GetClientRect(hWnd, &rect);
			rect.right += rect.left + pos.x;
			rect.bottom += rect.top + pos.y;
			rect.left = pos.x;
			rect.top = pos.y;

			bool succeeded = Window::SetStyleFlag(hWnd, WS_BORDER | WS_CAPTION, data->enabled);
			
			bool adjusted = AdjustWindowRectEx(&rect, Window::GetStyle(hWnd), FALSE, Window::GetExStyle(hWnd));
			if (adjusted) {
				SetWindowPos(hWnd, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOREPOSITION);
			}
			data->result = succeeded;
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = enabled;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		bool succeeded = job->GetDataAs<_NWWSetStyleFlag>()->result;
		return succeeded;
	}

	bool Window::IsResizable() const {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWHasStyleFlag>();
			bool result = Window::HasStyleFlag(data->hWnd, WS_SIZEBOX);
			data->result = result;
		};
		_NWWHasStyleFlag data = {};
		data.hWnd = hWnd;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);
		js->WaitJob(job);

		bool result = job->GetDataAs<_NWWHasStyleFlag>()->result;
		return result;
	}
	bool Window::SetResizable(bool resizable) {
		ERR_ASSERT(IsValid(), u8"The window is not valid!", return false);

		auto func = [](Job* job) {
			auto data = job->GetDataAs<_NWWSetStyleFlag>();
			bool succeeded = Window::SetStyleFlag(data->hWnd, WS_SIZEBOX, data->enabled);
			data->result = succeeded;
		};
		_NWWSetStyleFlag data = {};
		data.hWnd = hWnd;
		data.enabled = resizable;

		auto js = ENGINEINST->GetJobSystem();
		auto job = js->AddJob(func, &data, sizeof(data), Job::Preference::Window);

		js->WaitJob(job);

		bool succeeded = job->GetDataAs<_NWWSetStyleFlag>()->result;
		return succeeded;
	}

	HWND Window::GetHWnd() const {
		return hWnd;
	}

	DWORD Window::GetStyle(HWND hWnd) {
		return GetWindowLongW(hWnd, GWL_STYLE);
	}
	DWORD Window::GetStyle() const {
		return GetStyle(hWnd);
	}

	DWORD Window::GetExStyle(HWND hWnd) {
		return GetWindowLongW(hWnd, GWL_EXSTYLE);
	}
	DWORD Window::GetExStyle() const {
		return GetExStyle(hWnd);
	}

	bool Window::HasStyleFlag(HWND hWnd,DWORD style) {
		return GetStyle(hWnd) & style;
	}
	bool Window::HasStyleFlag(DWORD style) const {
		return HasStyleFlag(hWnd, style);
	}

	bool Window::SetStyleFlag(HWND hWnd, DWORD style, bool enabled) {
		DWORD s = GetStyle(hWnd);
		if (enabled) {
			s |= style;
		} else {
			s &= ~style;
		}
		SetWindowLongW(hWnd, GWL_STYLE, s);
		return true;
	}
	bool Window::SetStyleFlag(DWORD style, bool enabled) {
		return SetStyleFlag(hWnd, style, enabled);
	}
}
