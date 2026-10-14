#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include <microsoft.ui.xaml.window.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <cmath>
#include "../assets/resource.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;
namespace fs = std::filesystem;

namespace {
fs::path workspace() {
    int count{}; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    fs::path selected;
    for (int i = 1; i + 1 < count; ++i) if (std::wstring_view(args[i]) == L"--workspace") selected = args[++i];
    LocalFree(args);
    if (!selected.empty()) return fs::absolute(selected);
    std::wstring exe(32768, L'\0'); auto n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())); exe.resize(n);
    auto folder = fs::path(exe).parent_path();
    auto candidate = folder;
    for (int i = 0; i < 5; ++i) {
        if (fs::exists(candidate / "workspace.json") || fs::exists(candidate / "bin" / "qemu-system-x86_64w.exe")
            || fs::exists(candidate / "bin" / "qemu-system-i386w.exe")) return candidate;
        auto parent = candidate.parent_path(); if (parent == candidate) break; candidate = parent;
    }
    return folder;
}
TextBlock label(const std::string& text, double size = 14) {
    TextBlock t; t.Text(to_hstring(text)); t.FontSize(size); t.TextWrapping(TextWrapping::Wrap); return t;
}
void openFolder(HWND hwnd, const fs::path& p) {
    auto result = ShellExecuteW(hwnd, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) throw std::runtime_error("无法打开目录");
}
}
namespace winrt::QemuHelper::implementation {
MainWindow::MainWindow() {
    InitializeComponent();
    check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd_));
    const auto module = GetModuleHandleW(nullptr);
    const auto dpi = GetDpiForWindow(hwnd_);
    for (const auto kind : {ICON_SMALL, ICON_BIG}) {
        const auto size = MulDiv(kind == ICON_SMALL ? 16 : 32, dpi, 96);
        const auto icon = LoadImageW(module, MAKEINTRESOURCEW(IDI_WINHELPER), IMAGE_ICON, size, size, LR_SHARED);
        if (icon) SendMessageW(hwnd_, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
    }
    MONITORINFO screen{sizeof(screen)};
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &screen)) {
        const auto area = screen.rcWork;
        const int width = std::min(MulDiv(1120, dpi, 96), static_cast<int>(area.right - area.left - 32));
        const int height = std::min(MulDiv(800, dpi, 96), static_cast<int>(area.bottom - area.top - 32));
        AppWindow().MoveAndResize({area.left + (area.right - area.left - width) / 2,
            area.top + (area.bottom - area.top - height) / 2, width, height});
    } else AppWindow().Resize({1240, 890});
    host_ = helper::makeHost(); manager_ = std::make_unique<helper::Manager>(workspace(), *host_);
    Refresh();
    Status("工作区已就绪", helper::utf8(manager_->root()));
    timer_ = DispatcherQueue().CreateTimer(); timer_.Interval(std::chrono::seconds(2));
    timer_.Tick([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) { if (!self->busy_) { try { self->UpdateState(); } catch (...) { self->Error(); } } } });
    timer_.Start();
    AppWindow().Closing([weak = get_weak()](auto&&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& e) {
        if (auto self = weak.get(); self && self->busy_) { e.Cancel(true); self->Status("正在完成操作", "请等待磁盘或启动操作完成后再关闭管理器。"); }
    });
    Closed([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->timer_.Stop(); });
}
void MainWindow::Status(const std::string& title, const std::string& message, bool error) {
    const auto safeMessage = helper::Json::parse(helper::Json(message).dump(-1, ' ', false, helper::Json::error_handler_t::replace)).get<std::string>();
    StatusBar().Title(to_hstring(title)); StatusBar().Message(to_hstring(safeMessage));
    StatusBar().Severity(error ? InfoBarSeverity::Error : InfoBarSeverity::Informational); StatusBar().IsOpen(true);
}
void MainWindow::Error() {
    try { throw; }
    catch (const hresult_error& e) { Status("操作未完成", to_string(e.message()), true); }
    catch (const std::exception& e) { Status("操作未完成", e.what(), true); }
    catch (...) { Status("操作未完成", "未知错误", true); }
}
ContentDialog MainWindow::Dialog(const wchar_t* title, const wchar_t* primary) {
    ContentDialog d; d.XamlRoot(RootGrid().XamlRoot()); d.Title(box_value(title));
    if (*primary) d.PrimaryButtonText(primary);
    d.CloseButtonText(L"取消"); d.DefaultButton(ContentDialogButton::Close); return d;
}
fire_and_forget MainWindow::Run(std::function<std::string()> job, std::function<void()> after) {
    auto lifetime = get_strong();
    if (busy_) co_return;
    busy_ = true; RootControl().IsEnabled(false); BusyProgress().Visibility(Visibility::Visible);
    Status("正在处理", "请稍候；磁盘操作可能需要一些时间。");
    apartment_context ui;
    co_await resume_background();
    std::string result, failure;
    try { result = job(); }
    catch (const std::exception& e) { failure = e.what(); }
    catch (...) { failure = "操作失败"; }
    co_await ui;
    busy_ = false; RootControl().IsEnabled(true); BusyProgress().Visibility(Visibility::Collapsed);
    try {
        Refresh();
        if (!failure.empty()) Status("操作未完成", failure, true);
        else { Status("已完成", result); if (after) after(); }
    } catch (...) { Error(); }
}
helper::Machine MainWindow::Selected() {
    if (selected_.empty()) throw std::runtime_error("请先选择一台虚拟机");
    return manager_->machine(selected_);
}
void MainWindow::Refresh() {
    std::vector<std::string> warnings;
    auto values = manager_->machines(&warnings);
    refreshing_ = true; machines_ = std::move(values); VmList().Items().Clear();
    int selectedIndex = -1;
    for (size_t i = 0; i < machines_.size(); ++i) {
        StackPanel item; item.Spacing(4); item.Padding({6, 10, 6, 10});
        auto title = label(machines_[i].name, 15); title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        item.Children().Append(title);
        auto details = label(std::to_string(machines_[i].memoryMiB) + " MiB  ·  " + std::to_string(machines_[i].cpus) + " CPU  ·  " + machines_[i].accelerator, 11);
        details.Opacity(.65); item.Children().Append(details); VmList().Items().Append(item);
        if (machines_[i].id == selected_) selectedIndex = static_cast<int>(i);
    }
    if (selectedIndex == -1 && !machines_.empty()) selectedIndex = 0;
    VmList().SelectedIndex(selectedIndex);
    selected_ = selectedIndex < 0 ? "" : machines_[selectedIndex].id;
    refreshing_ = false; Populate();
    if (!warnings.empty()) Status("部分配置无法读取", warnings.front(), true);
}
void MainWindow::Populate() {
    populating_ = true;
    const bool empty = selected_.empty();
    EmptyPanel().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
    DetailsPanel().Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
    RenderSnapshots({}); if (empty) { populating_ = false; return; }
    auto vm = Selected();
    VmTitle().Text(to_hstring(vm.name)); VmSubtitle().Text(to_hstring("WINDOWS XP  /  " + vm.id.substr(0, 8)));
    NameBox().Text(to_hstring(vm.name)); MemoryBox().Value(vm.memoryMiB); CpuBox().SelectedIndex(vm.cpus - 1);
    AccelBox().SelectedIndex(vm.accelerator == "whpx" ? 1 : 0);
    NetworkToggle().IsOn(vm.network); SoundToggle().IsOn(vm.sound); UsbToggle().IsOn(vm.usb);
    IsoBox().Text(to_hstring(vm.iso)); DiskPathBox().Text(to_hstring(helper::utf8(manager_->disk(vm.id))));
    populating_ = false;
    UpdateState();
}
void MainWindow::UpdateState() {
    if (selected_.empty()) return;
    auto state = manager_->state(selected_);
    const bool stopped = state == helper::State::Stopped, running = state == helper::State::Running;
    StateText().Text(stopped ? L"已关机" : running ? L"运行中" : L"状态待确认");
    NameBox().IsEnabled(stopped); MemoryBox().IsEnabled(stopped); CpuBox().IsEnabled(stopped); AccelBox().IsEnabled(stopped);
    NetworkToggle().IsEnabled(stopped); SoundToggle().IsEnabled(stopped); UsbToggle().IsEnabled(stopped || running); SaveButton().IsEnabled(stopped);
    BootCdCheck().IsEnabled(stopped); StartButton().IsEnabled(stopped);
    ShutdownButton().IsEnabled(running); StopButton().IsEnabled(running); CreateSnapshotButton().IsEnabled(stopped);
    if (!stopped && !snapshots_.empty()) RenderSnapshots({});
}
void MainWindow::OnSelection(IInspectable const&, SelectionChangedEventArgs const&) {
    if (refreshing_ || busy_) return;
    try { auto i = VmList().SelectedIndex(); selected_ = i < 0 ? "" : machines_.at(i).id; BootCdCheck().IsChecked(false); Populate(); } catch (...) { Error(); }
}
void MainWindow::OnRefresh(IInspectable const&, RoutedEventArgs const&) { if (!busy_) { try { Refresh(); } catch (...) { Error(); } } }
fs::path MainWindow::Pick(bool save, bool folder, const wchar_t* filter, const wchar_t* name) {
    com_ptr<IFileDialog> dialog;
    if (save) dialog = create_instance<IFileSaveDialog>(CLSID_FileSaveDialog).as<IFileDialog>();
    else dialog = create_instance<IFileOpenDialog>(CLSID_FileOpenDialog).as<IFileDialog>();
    DWORD flags{}; check_hresult(dialog->GetOptions(&flags));
    check_hresult(dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : save ? FOS_PATHMUSTEXIST : FOS_FILEMUSTEXIST)));
    if (!folder) { COMDLG_FILTERSPEC spec[]{{L"支持的文件", filter}, {L"所有文件", L"*.*"}}; dialog->SetFileTypes(2, spec); }
    if (*name) dialog->SetFileName(name);
    if (save) dialog->SetDefaultExtension(L"qcow2");
    auto hr = dialog->Show(hwnd_); if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {}; check_hresult(hr);
    com_ptr<IShellItem> item; check_hresult(dialog->GetResult(item.put()));
    PWSTR raw{}; check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)); fs::path result(raw); CoTaskMemFree(raw); return result;
}
void MainWindow::OnCreate(IInspectable const&, RoutedEventArgs const&) { CreateDialog(false); }
void MainWindow::OnImport(IInspectable const&, RoutedEventArgs const&) { CreateDialog(true); }
fire_and_forget MainWindow::CreateDialog(bool import) {
    auto lifetime = get_strong();
    try {
        fs::path source;
        if (import) { source = Pick(false, false, L"*.qcow2;*.img;*.raw;*.vmdk;*.vdi;*.vhd;*.vhdx"); if (source.empty()) co_return; }
        auto dialog = Dialog(import ? L"导入现有磁盘" : L"新建虚拟机", import ? L"转换并导入" : L"创建虚拟机");
        StackPanel content; content.Spacing(14); content.Width(480);
        TextBox name; name.Header(box_value(L"虚拟机名称")); name.Text(L"Windows XP"); name.MaxLength(60); content.Children().Append(name);
        NumberBox size; size.Header(box_value(L"硬盘容量 / GiB（按需占用空间）")); size.Minimum(1); size.Maximum(2048); size.Value(32); size.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
        TextBox iso; iso.Header(box_value(L"安装 ISO（可稍后选择）"));
        if (!import) {
            content.Children().Append(size);
            const auto support = manager_->root() / "support";
            if (fs::is_directory(support)) for (const auto& e : fs::directory_iterator(support)) if (e.path().extension() == ".iso") { iso.Text(to_hstring(manager_->portable(e.path()))); break; }
            content.Children().Append(iso);
            Button browse; browse.Content(box_value(L"选择安装光盘")); browse.Click([this, iso](auto&&, auto&&) { try { auto p = Pick(false, false, L"*.iso"); if (!p.empty()) iso.Text(to_hstring(manager_->portable(p))); } catch (...) { Error(); } }); content.Children().Append(browse);
            content.Children().Append(label("默认：1 GiB 内存、2 核、TCG、IDE、AC97。磁盘实际占用会随使用增长。", 12));
        } else content.Children().Append(label("将当前磁盘内容转换为独立 qcow2 副本，原文件保留。不会导入原虚拟机的快照历史。\n\n" + helper::utf8(source), 12));
        dialog.Content(content);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        helper::Machine vm; vm.name = to_string(name.Text()); vm.iso = to_string(iso.Text());
        if (!std::isfinite(size.Value())) throw std::runtime_error("请输入磁盘容量");
        auto created = std::make_shared<std::string>(); const int gib = static_cast<int>(size.Value());
        Run([this, vm, gib, source, created] { auto value = manager_->create(vm, gib, source); *created = value.id; return "虚拟机已创建，可选择光盘后启动。"; },
            [this, created, vm, import] { selected_ = *created; Refresh(); BootCdCheck().IsChecked(!import && !vm.iso.empty()); });
    } catch (...) { Error(); }
}
void MainWindow::OnSave(IInspectable const&, RoutedEventArgs const&) {
    try {
        auto vm = Edited();
        Run([this, vm] { manager_->save(vm); return "硬件配置已保存。"; });
    } catch (...) { Error(); }
}
void MainWindow::OnMouseChanged(IInspectable const&, RoutedEventArgs const&) {
    if (populating_ || refreshing_ || busy_ || selected_.empty() || !manager_) return;
    try {
        auto id = Selected().id; const auto usb = UsbToggle().IsOn();
        // Offline changes are saved together with the other hardware fields.
        if (manager_->state(id) == helper::State::Stopped) return;
        Run([this, id, usb] { manager_->changeMouse(id, usb); return usb ? "已切换到 USB 鼠标。" : "已切换到 PS/2 鼠标。"; });
    } catch (...) { Error(); }
}
helper::Machine MainWindow::Edited() {
    auto vm = Selected(); vm.name = to_string(NameBox().Text());
    if (!std::isfinite(MemoryBox().Value())) throw std::runtime_error("请输入内存大小");
    vm.memoryMiB = static_cast<int>(MemoryBox().Value()); vm.cpus = CpuBox().SelectedIndex() + 1;
    vm.accelerator = AccelBox().SelectedIndex() == 1 ? "whpx" : "tcg";
    vm.network = NetworkToggle().IsOn(); vm.sound = SoundToggle().IsOn(); vm.usb = UsbToggle().IsOn();
    helper::validate(vm); return vm;
}
void MainWindow::OnStart(IInspectable const&, RoutedEventArgs const&) {
    try {
        auto vm = Edited(); helper::BootOptions boot; boot.iso = to_string(IsoBox().Text());
        boot.bootCdOnce = BootCdCheck().IsChecked().GetBoolean();
        Run([this, vm, boot] { manager_->save(vm); manager_->start(vm.id, boot); return "虚拟机已启动；关闭管理器不会关闭虚拟机。"; });
    } catch (...) { Error(); }
}
void MainWindow::OnShutdown(IInspectable const&, RoutedEventArgs const&) {
    try { auto id = Selected().id; Run([this, id] { manager_->shutdown(id); return "已发送关机请求，请等待客体完成关机。"; }); } catch (...) { Error(); }
}
fire_and_forget MainWindow::ConfirmAction(std::string title, std::string message, std::function<std::string()> job) {
    auto lifetime = get_strong();
    try { auto d = Dialog(to_hstring(title).c_str(), L"确认"); d.Content(label(message)); if (co_await d.ShowAsync() == ContentDialogResult::Primary) Run(std::move(job)); }
    catch (...) { Error(); }
}
void MainWindow::OnStop(IInspectable const&, RoutedEventArgs const&) {
    try { auto id = Selected().id; ConfirmAction("强制断电", "这相当于拔掉电源，未保存的数据会丢失。仅在客体无法正常关机时使用。", [this, id] { manager_->shutdown(id, true); return "已发送强制断电请求。"; }); } catch (...) { Error(); }
}
void MainWindow::OnBrowseIso(IInspectable const&, RoutedEventArgs const&) {
    try { auto file = Pick(false, false, L"*.iso"); if (!file.empty()) IsoBox().Text(to_hstring(manager_->portable(file))); } catch (...) { Error(); }
}
void MainWindow::OnToolsIso(IInspectable const&, RoutedEventArgs const&) {
    try { IsoBox().Text(to_hstring(manager_->settings().toolsIso)); } catch (...) { Error(); }
}
void MainWindow::OnMountIso(IInspectable const&, RoutedEventArgs const&) {
    try { auto id = Selected().id; auto iso = to_string(IsoBox().Text()); Run([this, id, iso] { manager_->changeIso(id, iso); return "光驱内容已更新。"; }); } catch (...) { Error(); }
}
void MainWindow::OnEjectIso(IInspectable const&, RoutedEventArgs const&) {
    try { auto id = Selected().id; Run([this, id] { manager_->changeIso(id, ""); return "光盘已弹出。"; }); } catch (...) { Error(); }
}
void MainWindow::RenderSnapshots(std::vector<helper::Snapshot> values) {
    snapshots_ = std::move(values); SnapshotList().Items().Clear();
    for (const auto& s : snapshots_) {
        StackPanel item; item.Spacing(3); item.Padding({8, 8, 8, 8}); item.Children().Append(label(s.name, 15));
        auto detail = label("ID " + s.id + (s.vmStateBytes ? "  ·  含内存状态（不支持操作）" : "  ·  磁盘快照"), 12); detail.Opacity(.6); item.Children().Append(detail);
        SnapshotList().Items().Append(item);
    }
}
void MainWindow::OnLoadSnapshots(IInspectable const&, RoutedEventArgs const&) {
    try {
        auto id = Selected().id; auto values = std::make_shared<std::vector<helper::Snapshot>>();
        Run([this, id, values] { *values = manager_->snapshots(id); return values->empty() ? "尚无快照，可在关机后创建。" : "快照列表已更新。"; }, [this, values] { RenderSnapshots(*values); });
    } catch (...) { Error(); }
}
void MainWindow::OnCreateSnapshot(IInspectable const&, RoutedEventArgs const&) { SnapshotDialog(); }
fire_and_forget MainWindow::SnapshotDialog() {
    auto lifetime = get_strong();
    try {
        auto id = Selected().id; auto d = Dialog(L"创建关机快照", L"创建快照");
        TextBox name; name.Header(box_value(L"快照名称")); name.PlaceholderText(L"例如：XP 安装完成、安装驱动之前"); name.Width(420); name.MaxLength(50); d.Content(name);
        if (co_await d.ShowAsync() != ContentDialogResult::Primary) co_return;
        auto title = to_string(name.Text()); auto values = std::make_shared<std::vector<helper::Snapshot>>();
        Run([this, id, title, values] { manager_->createSnapshot(id, title); *values = manager_->snapshots(id); return "快照已创建。"; }, [this, values] { RenderSnapshots(*values); });
    } catch (...) { Error(); }
}
void MainWindow::OnRestoreSnapshot(IInspectable const&, RoutedEventArgs const&) {
    try {
        auto id = Selected().id; auto i = SnapshotList().SelectedIndex(); if (i < 0) throw std::runtime_error("请先选择快照"); auto s = snapshots_.at(i);
        ConfirmAction("恢复快照", "将磁盘恢复到“" + s.name + "”。当前磁盘状态会先保存为 before-restore 快照；硬件设置与 ISO 不变。",
            [this, id, s] { return "已恢复；恢复前状态保存在快照 " + manager_->restoreSnapshot(id, s.id) + "。点击刷新快照查看。"; });
    } catch (...) { Error(); }
}
void MainWindow::OnDeleteSnapshot(IInspectable const&, RoutedEventArgs const&) {
    try {
        auto id = Selected().id; auto i = SnapshotList().SelectedIndex(); if (i < 0) throw std::runtime_error("请先选择快照"); auto s = snapshots_.at(i);
        ConfirmAction("删除快照", "删除“" + s.name + "”后无法再恢复到该状态。当前磁盘内容不会改变。",
            [this, id, s] { manager_->deleteSnapshot(id, s.id); return "快照已删除。点击刷新快照查看列表。"; });
    } catch (...) { Error(); }
}
void MainWindow::OnExport(IInspectable const&, RoutedEventArgs const&) {
    try { auto id = Selected().id; auto p = Pick(true, false, L"*.qcow2", L"windows-xp-backup.qcow2"); if (!p.empty()) Run([this, id, p] { manager_->exportDisk(id, p); return "当前磁盘已导出到 " + helper::utf8(p); }); } catch (...) { Error(); }
}
void MainWindow::OnRemove(IInspectable const&, RoutedEventArgs const&) {
    try { auto vm = Selected(); ConfirmAction("从列表移除", "只停用“" + vm.name + "”的配置记录，保留磁盘与快照。虚拟机必须处于关机状态。",
        [this, vm] { manager_->removeFromList(vm.id); return "已从列表移除；文件保留在 " + helper::utf8(manager_->directory(vm.id)); }); } catch (...) { Error(); }
}
void MainWindow::OnOpenWorkspace(IInspectable const&, RoutedEventArgs const&) { try { openFolder(hwnd_, manager_->root()); } catch (...) { Error(); } }
void MainWindow::OnOpenMachine(IInspectable const&, RoutedEventArgs const&) { try { openFolder(hwnd_, manager_->directory(Selected().id)); } catch (...) { Error(); } }
void MainWindow::OnOpenLogs(IInspectable const&, RoutedEventArgs const&) { try { const auto folder = manager_->directory(Selected().id) / "logs"; fs::create_directories(folder); openFolder(hwnd_, folder); } catch (...) { Error(); } }
}
