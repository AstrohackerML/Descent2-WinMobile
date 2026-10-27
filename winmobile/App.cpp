// UWP app shell: owns the window, feeds touch and the back button to the
// game, gets the game data in place and runs Descent on its own thread. On a
// PC it also feeds the keyboard and the mouse.

#include <atomic>
#include <collection.h>
#include <ppltasks.h>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "gles_d3d11.h"
#include "platform.h"

using namespace concurrency;
using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Devices::Input;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::System;
using namespace Windows::System::Profile;
using namespace Windows::System::Threading;
using namespace Windows::System::UserProfile;
using namespace Windows::UI::Popups;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::Text::Core;
using namespace Windows::UI::ViewManagement;

#define KEY_ESC 0x01
#define KEY_BACKSP 0x0E
#define KEY_ENTER 0x1C

namespace {

std::string Document_path_utf8, Cache_path_utf8, Data_path_utf8;

// The mouse's movement while flying, taken by the game thread every frame
std::atomic<int> Mouse_dx{0}, Mouse_dy{0};

// A PC, as opposed to the phone: keyboard, mouse and a window of any shape
bool is_desktop() {
	return AnalyticsInfo::VersionInfo->DeviceFamily == L"Windows.Desktop";
}

// The files the game reads from the player's copy of Descent II
const wchar_t *const kDataExtensions[] = {
	L".hog", L".pig", L".ham", L".s11", L".s22", L".mvl", L".mn2",
};

std::string to_utf8(String ^s) {
	int n = WideCharToMultiByte(CP_UTF8, 0, s->Data(), -1, nullptr, 0, nullptr, nullptr);
	std::string out(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, s->Data(), -1, &out[0], n, nullptr, nullptr);
	out.resize(n - 1);
	return out;
}

bool is_data_file(const std::wstring &name) {
	size_t dot = name.rfind(L'.');
	if (dot == std::wstring::npos) {
		return false;
	}
	std::wstring ext = name.substr(dot);
	for (const wchar_t *e : kDataExtensions) {
		if (ext == e) {
			return true;
		}
	}
	return false;
}

void press_key(unsigned char key) {
	key_handler(key, true);
	key_handler(key, false);
}

// How the window is turned relative to the display, as DXGI wants it
int display_rotation() {
	DisplayInformation ^info = DisplayInformation::GetForCurrentView();
	bool native_portrait = info->NativeOrientation == DisplayOrientations::Portrait;
	switch (info->CurrentOrientation) {
		case DisplayOrientations::Landscape:
			return native_portrait ? DXGI_MODE_ROTATION_ROTATE270 : DXGI_MODE_ROTATION_IDENTITY;
		case DisplayOrientations::LandscapeFlipped:
			return native_portrait ? DXGI_MODE_ROTATION_ROTATE90 : DXGI_MODE_ROTATION_ROTATE180;
		case DisplayOrientations::Portrait:
			return native_portrait ? DXGI_MODE_ROTATION_IDENTITY : DXGI_MODE_ROTATION_ROTATE90;
		case DisplayOrientations::PortraitFlipped:
			return native_portrait ? DXGI_MODE_ROTATION_ROTATE180 : DXGI_MODE_ROTATION_ROTATE270;
		default:
			return DXGI_MODE_ROTATION_IDENTITY;
	}
}

// Files the game cannot start without, as in the Android port
const char *const kRequiredFiles[] = {
	"descent2.hog", "descent2.ham", "alien1.pig", "alien2.pig", "fire.pig", "groupa.pig",
	"ice.pig", "water.pig",
};

// Either of these sound files will do
const char *const kSoundFiles[] = {"descent2.s22", "descent2.s11"};

bool data_file_exists(const char *name) {
	std::string path = Data_path_utf8 + "\\" + name;
	DWORD attrs = GetFileAttributesA(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Names of the required files the data folder does not have
std::vector<std::wstring> missing_files() {
	std::vector<std::wstring> missing;
	for (const char *name : kRequiredFiles) {
		if (!data_file_exists(name)) {
			missing.push_back(std::wstring(name, name + strlen(name)));
		}
	}
	if (!data_file_exists(kSoundFiles[0]) && !data_file_exists(kSoundFiles[1])) {
		const char *name = kSoundFiles[0];
		missing.push_back(std::wstring(name, name + strlen(name)));
	}
	return missing;
}

bool have_data() {
	return missing_files().empty();
}

// The player's language, not the app's: the manifest lists English only
bool prefers_russian() {
	IVectorView<String ^> ^languages = GlobalizationPreferences::Languages;
	return languages->Size > 0 && wcsncmp(languages->GetAt(0)->Data(), L"ru", 2) == 0;
}

// Copies the data files from the folder the player picked and its
// subfolders, lowercasing the names the way the game looks them up
task<void> copy_data(StorageFolder ^from, StorageFolder ^to) {
	return create_task(from->GetFilesAsync()).then([to](IVectorView<StorageFile ^> ^files) {
		task<void> chain = task_from_result();
		for (StorageFile ^file : files) {
			std::wstring name = file->Name->Data();
			for (wchar_t &c : name) {
				c = towlower(c);
			}
			if (is_data_file(name)) {
				String ^target = ref new String(name.c_str());
				chain = chain.then([file, to, target] {
					return create_task(file->CopyAsync(to, target,
													   NameCollisionOption::ReplaceExisting));
				}).then([](StorageFile ^) {});
			}
		}
		return chain;
	}).then([from] {
		return create_task(from->GetFoldersAsync());
	}).then([to](IVectorView<StorageFolder ^> ^folders) {
		task<void> chain = task_from_result();
		for (StorageFolder ^folder : folders) {
			chain = chain.then([folder, to] { return copy_data(folder, to); });
		}
		return chain;
	});
}

}

ref class App sealed : public IFrameworkView {
public:
	virtual void Initialize(CoreApplicationView ^view) {
		view->Activated += ref new TypedEventHandler<CoreApplicationView ^, IActivatedEventArgs ^>(
				this, &App::OnActivated);
	}

	virtual void SetWindow(CoreWindow ^window) {
		window_ = window;
		dispatcher_ = window->Dispatcher;
		window->PointerPressed += ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(
				this, &App::OnPointerPressed);
		window->PointerMoved += ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(
				this, &App::OnPointerMoved);
		window->PointerReleased += ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(
				this, &App::OnPointerReleased);
		window->SizeChanged += ref new TypedEventHandler<CoreWindow ^, WindowSizeChangedEventArgs ^>(
				this, &App::OnSizeChanged);
		window->KeyDown += ref new TypedEventHandler<CoreWindow ^, KeyEventArgs ^>(
				this, &App::OnKeyDown);
		DisplayInformation::GetForCurrentView()->OrientationChanged +=
				ref new TypedEventHandler<DisplayInformation ^, Object ^>(
						this, &App::OnOrientationChanged);
		SystemNavigationManager::GetForCurrentView()->BackRequested +=
				ref new EventHandler<BackRequestedEventArgs ^>(this, &App::OnBackRequested);

		DisplayInformation::AutoRotationPreferences =
				DisplayOrientations::Landscape | DisplayOrientations::LandscapeFlipped;
		ApplicationView::PreferredLaunchWindowingMode = ApplicationViewWindowingMode::FullScreen;
		ApplicationView::GetForCurrentView()->TryEnterFullScreenMode();
		Display_scale = (float) DisplayInformation::GetForCurrentView()->RawPixelsPerViewPixel;
		CreateEditContext();
		the_app = this;

		desktop_ = is_desktop();
		if (desktop_) {
			SetUpDesktop();
		}
	}

	virtual void Load(String ^entryPoint) {
	}

	virtual void Run() {
		CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(
				CoreProcessEventsOption::ProcessUntilQuit);
	}

	virtual void Uninitialize() {
	}

private:
	// A PC plays with the keyboard and the mouse as the original game did; the
	// touch controls only show up once the screen is touched
	void SetUpDesktop() {
		gles_set_turn_portrait(false);
		Relative_mouse = 1;
		Touch_controls_shown = 0;
		arrow_ = window_->PointerCursor;

		dispatcher_->AcceleratorKeyActivated +=
				ref new TypedEventHandler<CoreDispatcher ^, AcceleratorKeyEventArgs ^>(
						this, &App::OnAcceleratorKey);
		window_->Activated += ref new TypedEventHandler<CoreWindow ^, WindowActivatedEventArgs ^>(
				this, &App::OnWindowActivated);
		MouseDevice::GetForCurrentView()->MouseMoved +=
				ref new TypedEventHandler<MouseDevice ^, MouseEventArgs ^>(this, &App::OnMouseMoved);

		// The game does not say when it starts or stops flying, so look often
		TimeSpan period;
		period.Duration = 1000000;  // 100 ms
		CoreDispatcher ^dispatcher = dispatcher_;
		flight_timer_ = ThreadPoolTimer::CreatePeriodicTimer(ref new TimerElapsedHandler(
				[this, dispatcher](ThreadPoolTimer ^) {
			dispatcher->RunAsync(CoreDispatcherPriority::Normal,
					ref new DispatchedHandler([this] { UpdateFlying(); }));
		}), period);
	}

	// While flying the mouse steers, so its pointer is hidden and its buttons
	// fire; in menus it is the pointer again
	void UpdateFlying() {
		bool flying = started_ && in_game_view();
		if (flying == flying_) {
			return;
		}
		flying_ = flying;
		window_->PointerCursor = flying ? nullptr : arrow_;
		Mouse_dx = 0;
		Mouse_dy = 0;
		if (!flying) {
			SetMouseButtons(false, false, false);
		}
	}

	void SetMouseButtons(bool left, bool right, bool middle) {
		const bool now[3] = {left, right, middle};
		for (int i = 0; i < 3; ++i) {
			if (mouse_buttons_[i] != now[i]) {
				mouse_buttons_[i] = now[i];
				mouse_button_handler(i, now[i]);
			}
		}
	}

	void OnMouseMoved(MouseDevice ^sender, MouseEventArgs ^args) {
		if (flying_) {
			Mouse_dx += args->MouseDelta.X;
			Mouse_dy += args->MouseDelta.Y;
		}
	}

	// Keys go to the game as the PC scan codes the original read, so the
	// controls sit where they did whatever the keyboard layout. Text being
	// typed goes through the edit context instead, see OnTextUpdating.
	void OnAcceleratorKey(CoreDispatcher ^sender, AcceleratorKeyEventArgs ^args) {
		CoreAcceleratorKeyEventType type = args->EventType;
		bool down = type == CoreAcceleratorKeyEventType::KeyDown ||
					type == CoreAcceleratorKeyEventType::SystemKeyDown;
		bool up = type == CoreAcceleratorKeyEventType::KeyUp ||
				  type == CoreAcceleratorKeyEventType::SystemKeyUp;
		CorePhysicalKeyStatus status = args->KeyStatus;
		unsigned char code = (unsigned char) ((status.ScanCode & 0x7F) |
											  (status.IsExtendedKey ? 0x80 : 0));
		if (!started_ || (status.ScanCode & 0x7F) == 0) {
			return;
		}
		if (down) {
			Touch_controls_shown = 0;
			if (!text_active_) {
				pressed_keys_.insert(code);
				key_handler(code, true);
			}
		} else if (up && pressed_keys_.erase(code)) {
			key_handler(code, false);
		}
	}

	// Nothing stays held down while another window has the keyboard
	void OnWindowActivated(CoreWindow ^sender, WindowActivatedEventArgs ^args) {
		if (args->WindowActivationState != CoreWindowActivationState::Deactivated) {
			return;
		}
		for (unsigned char code : pressed_keys_) {
			key_handler(code, false);
		}
		pressed_keys_.clear();
		SetMouseButtons(false, false, false);
	}

	void OnActivated(CoreApplicationView ^view, IActivatedEventArgs ^args) {
		window_->Activate();
		if (started_ || preparing_) {
			return;
		}
		preparing_ = true;

		StorageFolder ^local = ApplicationData::Current->LocalFolder;
		Document_path_utf8 = to_utf8(local->Path);
		Cache_path_utf8 = to_utf8(ApplicationData::Current->TemporaryFolder->Path);
		Data_path_utf8 = Document_path_utf8 + "\\data";
		Document_path = Document_path_utf8.c_str();
		Cache_path = Cache_path_utf8.c_str();
		Data_path = Data_path_utf8.c_str();

		if (have_data()) {
			data_ready_ = true;
			TryStart();
			return;
		}

		PickData();
	}

	// Asks for the folder with the player's copy of Descent II, as the Android
	// port did, and copies the data from it. Quits if the player gives up.
	void PickData() {
		StorageFolder ^local = ApplicationData::Current->LocalFolder;
		FolderPicker ^picker = ref new FolderPicker();
		picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
		picker->FileTypeFilter->Append(L"*");
		create_task(picker->PickSingleFolderAsync()).then([local](StorageFolder ^folder) -> task<bool> {
			if (!folder) {
				return task_from_result(false);
			}
			return create_task(local->CreateFolderAsync(L"data",
					CreationCollisionOption::OpenIfExists)).then([folder](StorageFolder ^data) {
				return copy_data(folder, data);
			}).then([] {
				return true;
			});
		}).then([this](task<bool> t) {
			bool picked = true;
			try {
				picked = t.get();
			} catch (Exception ^) {
				// A failed copy shows up as missing files below
			}
			if (!picked) {
				CoreApplication::Exit();
				return;
			}
			std::vector<std::wstring> missing = missing_files();
			if (!missing.empty()) {
				ShowMissing(missing);
				return;
			}
			data_ready_ = true;
			TryStart();
		}, task_continuation_context::use_current());
	}

	// Says which files the picked folder lacked and offers another folder or quitting
	void ShowMissing(const std::vector<std::wstring> &missing) {
		bool ru = prefers_russian();
		std::wstring text = ru ? L"\u0412 \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u043e\u0439 "
								 L"\u043f\u0430\u043f\u043a\u0435 \u043d\u0435\u0442 "
								 L"\u0444\u0430\u0439\u043b\u043e\u0432 Descent II:\n"
							   : L"The folder you picked lacks these Descent II files:\n";
		for (const std::wstring &name : missing) {
			text += L"\n" + name;
		}
		text += ru ? L"\n\n\u041d\u0443\u0436\u043d\u0430 \u043f\u0430\u043f\u043a\u0430 "
					 L"\u0441 \u043f\u043e\u043b\u043d\u043e\u0439 \u0432\u0435\u0440\u0441\u0438\u0435\u0439 "
					 L"Descent II."
				   : L"\n\nPick the folder with the full version of Descent II.";

		MessageDialog ^dialog = ref new MessageDialog(
				ref new String(text.c_str()),
				ru ? L"\u041d\u0435 \u0445\u0432\u0430\u0442\u0430\u0435\u0442 "
					 L"\u0444\u0430\u0439\u043b\u043e\u0432"
				   : L"Missing game files");
		dialog->Commands->Append(ref new UICommand(
				ru ? L"\u0412\u044b\u0431\u0440\u0430\u0442\u044c \u0434\u0440\u0443\u0433\u0443\u044e "
					 L"\u043f\u0430\u043f\u043a\u0443"
				   : L"Pick another folder", nullptr, 0));
		dialog->Commands->Append(ref new UICommand(
				ru ? L"\u0412\u044b\u0439\u0442\u0438" : L"Quit", nullptr, 1));
		dialog->DefaultCommandIndex = 0;
		dialog->CancelCommandIndex = 1;
		create_task(dialog->ShowAsync()).then([this](IUICommand ^command) {
			if (command && safe_cast<int>(command->Id) == 0) {
				PickData();
			} else {
				CoreApplication::Exit();
			}
		}, task_continuation_context::use_current());
	}

	// Tells the renderer what shape the window is now. Windows may keep the
	// window portrait despite the landscape preference.
	void UpdateWindow() {
		Rect bounds = window_->Bounds;
		gles_set_window((int) (bounds.Width * Display_scale + 0.5f),
						(int) (bounds.Height * Display_scale + 0.5f), display_rotation());
	}

	// Starts the game once the data is in place. The game's frame is
	// landscape whatever shape the window has.
	void TryStart() {
		if (started_ || !data_ready_) {
			return;
		}
		started_ = true;
		UpdateWindow();
		Rect bounds = window_->Bounds;
		int w = (int) (max(bounds.Width, bounds.Height) * Display_scale + 0.5f);
		int h = (int) (min(bounds.Width, bounds.Height) * Display_scale + 0.5f);
		Agile<CoreWindow> window(window_);
		std::thread([window, w, h] {
			// XAudio2 under the sound code needs COM on this thread
			CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			char program[] = "", width_arg[] = "-width", height_arg[] = "-height";
			char ws[16], hs[16];
			sprintf_s(ws, "%d", w);
			sprintf_s(hs, "%d", h);
			char *args[] = {program, width_arg, ws, height_arg, hs};

			if (gles_init(reinterpret_cast<IUnknown *>(window.Get()), w, h)) {
				init_buttons(w, h);
				descent_main(5, args);
			}
			CoreApplication::Exit();
		}).detach();
	}

	void OnSizeChanged(CoreWindow ^sender, WindowSizeChangedEventArgs ^args) {
		UpdateWindow();
	}

	void OnOrientationChanged(DisplayInformation ^sender, Object ^args) {
		UpdateWindow();
	}

	void OnBackRequested(Object ^sender, BackRequestedEventArgs ^args) {
		args->Handled = true;
		if (started_) {
			key_handler(KEY_ESC, true);
			key_handler(KEY_ESC, false);
		}
	}

	// Touches go to the on-screen controls first, the rest act as the mouse,
	// as in the Android port. The controls expect small pointer ids.
	void HandlePointer(int action, PointerEventArgs ^args) {
		PointerPoint ^point = args->CurrentPoint;
		unsigned int id = point->PointerId;
		float x, y;
		gles_window_to_frame(point->Position.X * Display_scale, point->Position.Y * Display_scale,
							 &x, &y);
		int slot;

		if (!started_) {
			return;
		}
		if (desktop_) {
			bool mouse = point->PointerDevice->PointerDeviceType == PointerDeviceType::Mouse;
			Touch_controls_shown = !mouse;
			if (mouse && flying_) {
				PointerPointProperties ^buttons = point->Properties;
				SetMouseButtons(buttons->IsLeftButtonPressed, buttons->IsRightButtonPressed,
								buttons->IsMiddleButtonPressed);
				return;
			}
		}
		auto it = slots_.find(id);
		if (it == slots_.end()) {
			if (action != TOUCH_DOWN) {
				return;
			}
			for (slot = 0; slot < MAX_TOUCHES; ++slot) {
				bool used = false;
				for (auto &s : slots_) {
					used |= s.second.slot == slot;
				}
				if (!used) {
					break;
				}
			}
			if (slot == MAX_TOUCHES) {
				return;
			}
			it = slots_.emplace(id, Touch{slot, x, y}).first;
		}
		Touch &touch = it->second;

		bool handled = touch_handler(action, touch.slot, x, y, touch.x, touch.y) != 0;
		if (!handled && action != TOUCH_MOVE && point->Properties->IsPrimary) {
			mouse_handler((short) x, (short) y, action == TOUCH_DOWN);
			// A click that starts the game is let go of while flying
			mouse_buttons_[0] = action == TOUCH_DOWN;
		} else {
			mouse_set_pos((short) x, (short) y);
		}

		touch.x = x;
		touch.y = y;
		if (action == TOUCH_UP) {
			slots_.erase(it);
		}
	}

	void OnPointerPressed(CoreWindow ^sender, PointerEventArgs ^args) {
		HandlePointer(TOUCH_DOWN, args);
	}

	void OnPointerMoved(CoreWindow ^sender, PointerEventArgs ^args) {
		HandlePointer(TOUCH_MOVE, args);
	}

	void OnPointerReleased(CoreWindow ^sender, PointerEventArgs ^args) {
		HandlePointer(TOUCH_UP, args);
	}

	// The soft keyboard. Its text is mirrored in text_ and every change is
	// passed on to the game as key presses, as the Android port did.
	void CreateEditContext() {
		edit_ = CoreTextServicesManager::GetForCurrentView()->CreateEditContext();
		edit_->InputPaneDisplayPolicy = CoreTextInputPaneDisplayPolicy::Manual;
		edit_->TextRequested += ref new TypedEventHandler<CoreTextEditContext ^,
				CoreTextTextRequestedEventArgs ^>(this, &App::OnTextRequested);
		edit_->SelectionRequested += ref new TypedEventHandler<CoreTextEditContext ^,
				CoreTextSelectionRequestedEventArgs ^>(this, &App::OnSelectionRequested);
		edit_->TextUpdating += ref new TypedEventHandler<CoreTextEditContext ^,
				CoreTextTextUpdatingEventArgs ^>(this, &App::OnTextUpdating);
	}

	void OnTextRequested(CoreTextEditContext ^sender, CoreTextTextRequestedEventArgs ^args) {
		CoreTextRange range = args->Request->Range;
		int end = min(range.EndCaretPosition, (int) text_.size());
		int start = min(range.StartCaretPosition, end);
		args->Request->Text = ref new String(text_.substr(start, end - start).c_str());
	}

	void OnSelectionRequested(CoreTextEditContext ^sender,
							  CoreTextSelectionRequestedEventArgs ^args) {
		CoreTextRange caret = {(int) text_.size(), (int) text_.size()};
		args->Request->Selection = caret;
	}

	// The game only edits at the end, so everything after the start of the
	// replaced range is erased and typed again
	void OnTextUpdating(CoreTextEditContext ^sender, CoreTextTextUpdatingEventArgs ^args) {
		CoreTextRange range = args->Range;
		int start = min(range.StartCaretPosition, (int) text_.size());
		int end = max(start, min(range.EndCaretPosition, (int) text_.size()));
		std::wstring retype = std::wstring(args->Text->Data()) + text_.substr(end);

		for (size_t i = start; i < text_.size(); ++i) {
			press_key(KEY_BACKSP);
		}
		text_.resize(start);
		for (wchar_t c : retype) {
			if (c >= 32 && c < 127) {
				press_key((unsigned char) c);
				text_ += c;
			}
		}
	}

	void OnKeyDown(CoreWindow ^sender, KeyEventArgs ^args) {
		// On a PC the keys are passed on in OnAcceleratorKey, except while typing
		if (!started_ || (desktop_ && !text_active_)) {
			return;
		}
		if (args->VirtualKey == VirtualKey::Enter) {
			press_key(KEY_ENTER);
		} else if (args->VirtualKey == VirtualKey::Escape) {
			press_key(KEY_ESC);
		} else if (args->VirtualKey == VirtualKey::Back && text_active_) {
			if (!text_.empty()) {
				CoreTextRange removed = {(int) text_.size() - 1, (int) text_.size()};
				text_.pop_back();
				CoreTextRange caret = {(int) text_.size(), (int) text_.size()};
				edit_->NotifyTextChanged(removed, 0, caret);
			}
			press_key(KEY_BACKSP);
		}
	}

internal:
	// Called from the game thread
	void SetTextActive(bool active) {
		text_active_ = active;
		dispatcher_->RunAsync(CoreDispatcherPriority::Normal,
				ref new DispatchedHandler([this, active] {
			if (active) {
				CoreTextRange all = {0, (int) text_.size()}, caret = {0, 0};
				text_.clear();
				edit_->NotifyTextChanged(all, 0, caret);
				edit_->NotifyFocusEnter();
				InputPane::GetForCurrentView()->TryShow();
			} else {
				edit_->NotifyFocusLeave();
				InputPane::GetForCurrentView()->TryHide();
			}
		}));
	}

	bool TextActive() {
		return text_active_;
	}

	static App ^the_app;

private:
	struct Touch {
		int slot;
		float x, y;
	};

	CoreWindow ^window_;
	CoreDispatcher ^dispatcher_;
	CoreTextEditContext ^edit_;
	std::wstring text_;
	std::atomic<bool> text_active_{false};
	std::map<unsigned int, Touch> slots_;
	bool preparing_ = false;
	bool data_ready_ = false;
	bool started_ = false;

	// PC only
	bool desktop_ = false;
	bool flying_ = false;
	bool mouse_buttons_[3] = {};
	std::set<unsigned char> pressed_keys_;
	CoreCursor ^arrow_;
	ThreadPoolTimer ^flight_timer_;
};

App ^App::the_app;

// The soft keyboard calls of the core, see newmenu.c and automap.c
extern "C" {

int textIsActive() {
	return App::the_app->TextActive();
}

void activateText() {
	App::the_app->SetTextActive(true);
}

void deactivateText() {
	App::the_app->SetTextActive(false);
}

void take_mouse_delta(int *dx, int *dy) {
	*dx = Mouse_dx.exchange(0);
	*dy = Mouse_dy.exchange(0);
}

}

ref class AppSource sealed : IFrameworkViewSource {
public:
	virtual IFrameworkView ^CreateView() {
		return ref new App();
	}
};

[MTAThread]
int main(Array<String ^> ^) {
	CoreApplication::Run(ref new AppSource());
	return 0;
}
