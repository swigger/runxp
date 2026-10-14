#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
namespace winrt::QemuHelper::implementation {
App::App() { InitializeComponent(); }
void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    try { window_ = make<MainWindow>(); window_.Activate(); }
    catch (const std::exception& e) { MessageBoxW(nullptr, to_hstring(e.what()).c_str(), L"QEMU 3dfx WinHelper", MB_ICONERROR); Exit(); }
    catch (const hresult_error& e) { MessageBoxW(nullptr, e.message().c_str(), L"QEMU 3dfx WinHelper", MB_ICONERROR); Exit(); }
}
}
