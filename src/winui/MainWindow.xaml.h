#pragma once
#include "MainWindow.g.h"
#include "core/Manager.h"

namespace winrt::QemuHelper::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void OnSelection(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnRefresh(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCreate(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnImport(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSave(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMouseChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnStart(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnShutdown(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnStop(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnBrowseIso(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnToolsIso(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMountIso(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnEjectIso(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLoadSnapshots(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCreateSnapshot(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRestoreSnapshot(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDeleteSnapshot(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnExport(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRemove(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOpenWorkspace(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOpenMachine(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnOpenLogs(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    std::unique_ptr<helper::Host> host_;
    std::unique_ptr<helper::Manager> manager_;
    std::vector<helper::Machine> machines_;
    std::vector<helper::Snapshot> snapshots_;
    std::string selected_;
    bool busy_ = false, refreshing_ = false, populating_ = true;
    HWND hwnd_{};
    Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
    void Refresh();
    void Populate();
    void UpdateState();
    helper::Machine Selected();
    helper::Machine Edited();
    void Status(const std::string& title, const std::string& message, bool error = false);
    void Error();
    void RenderSnapshots(std::vector<helper::Snapshot> values);
    helper::fs::path Pick(bool save, bool folder, const wchar_t* filter, const wchar_t* name = L"");
    Microsoft::UI::Xaml::Controls::ContentDialog Dialog(const wchar_t* title, const wchar_t* primary);
    fire_and_forget Run(std::function<std::string()> job, std::function<void()> after = {});
    fire_and_forget CreateDialog(bool import);
    fire_and_forget ConfirmAction(std::string title, std::string message, std::function<std::string()> job);
    fire_and_forget SnapshotDialog();
};
}
namespace winrt::QemuHelper::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
