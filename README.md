# DShare

**DShare** 是一款基于 **DTK（Deepin Tool Kit）** 专为 **deepin 桌面环境**
打造的文件共享软件。它把本机的一个目录（`~/myshare`）通过局域网共享出去：
既提供原生 DTK 图形界面在本地管理文件，也内置一个 HTTP/HTTPS 服务，让同局域网
内的其他设备通过浏览器或本程序直接浏览、下载、上传文件。

> 采用 DTK6 开发，深度集成 deepin 的视觉风格与桌面组件（窗口、控件、主题、
> 提示框、日志等），是 deepin / UOS v25 桌面下体验一致的文件共享工具。

> **English version is available at the bottom of this file.**

---

## 功能特性

- **本地文件管理**：基于 DTK 的图形界面，浏览共享目录 `~/myshare`，支持
  新建文件夹、复制/粘贴、拖拽移动/复制、删除、双击打开。
- **Web 文件共享**：程序启动时自动在 **5000 端口** 开启服务，可访问地址显示在
  窗口底部状态栏（随「访问需授权」开关在 `http://` / `https://` 间切换）：
  - 浏览目录（带路径导航、文件大小展示）
  - 下载单个文件
  - 通过网页表单上传文件、新建文件夹
  - **未开启授权时使用明文 HTTP**，浏览器直接访问、无自签证书告警；
  - **开启授权时使用 HTTPS**（自动用 `openssl` 生成自签名证书并写入本机 LAN IP
    到 SAN），以加密保护授权凭据，浏览器会提示证书不受信任，选择「继续访问」即可。
- **局域网设备发现**：基于多播（UDP 端口 5001 / 组播组 `239.255.42.99`）自动
  发现同局域网内其它运行本程序的 deepin 机器。可按**机器名正则**发起查找，避免
  手动记 IP。
- **远程目录浏览与传输**：在地址栏「机器」下拉中选择另一台设备即可只读浏览其
  共享目录：
  - 双击文件 → 下载到本机缓存并默认打开
  - 把远程文件拖到本地视图/系统文件管理器 → 下载
  - 把本地文件拖入远程视图 → 上传到对方共享目录
- **访问授权（可选）**：顶部「访问需授权」开关开启后，任何设备访问你的共享都
  需经你**手动批准**。授权请求会弹出提示框显示对方机器名与 IP；浏览器端通过
  自动握手 + Cookie 完成授权，桌面客户端通过 `AuthManager` 自动完成握手，无需
  用户记忆任何口令。
- **手机扫码访问**：地址栏「机器」下拉框右侧有一个二维码，实时对应**本机当前
  共享目录**的网页地址。手机扫码即可在浏览器打开该目录（未开启授权为明文 HTTP，
  无证书告警）；**点击二维码弹出大图预览**（含地址与「复制地址」按钮）。进入
  远程浏览时该二维码自动禁用。「添加客户端」入口已并入下拉框的末项
  「+ 添加客户端…」，选中即可查找并连接其它设备。

---

## 编译依赖

| 依赖 | 说明 |
|------|------|
| CMake >= 3.13 | 构建系统 |
| Qt6 `Core` / `Widgets` / `Network` / `HttpServer` | 应用框架与 HTTP 服务 |
| DTK6 `Core` / `Gui` / `Widget` / `Log` | deepin 桌面组件与日志（本程序基于此构建） |
| `openssl` | 生成自签名证书（运行期使用其命令行） |
| C++17 编译器（g++ / clang） | — |

> 适用于 deepin / UOS v25（DTK6）环境。

---

## 编译与安装

```bash
# 在项目根目录
mkdir -p build && cd build
cmake ..
make -j"$(nproc)"

# 运行（未安装时可直接执行构建产物）
./dshare

# 可选：安装到系统
sudo make install      # 可执行文件 -> /usr/bin，桌面项 -> /usr/share/applications
```

安装后也可从应用菜单启动（桌面项名称为「DShare」）。

---

## 使用方法

### 1. 本地管理共享目录

启动后默认进入本机 `~/myshare`（不存在会自动创建）。通过工具栏即可：

- `返回上级` / `新建文件夹` / `刷新` / `复制` / `粘贴`
- 右键菜单：新建文件夹、刷新、复制、粘贴、删除、打开
- 选中文件后在本窗口内拖拽 = 移动，从外部（如系统文件管理器）拖入 = 复制

### 2. 通过浏览器共享 / 访问

- **本机访问**：浏览器打开状态栏显示的地址（如 <http://localhost:5000>）。
- **局域网访问**：窗口底部状态栏会显示当前可访问地址——**未开启授权** 为
  `http://<你的IP>:5000`，**开启授权** 为 `https://<你的IP>:5000`，
  把该地址发给同局域网的其他人即可。
- 在网页中可浏览目录、点击文件下载、使用表单上传文件或新建文件夹。
- 网页上传会实时显示进度与速度，上传期间禁用上传按钮，避免重复提交。
- 仅当「访问需授权」开启（HTTPS）时，浏览器会提示自签名证书不受信任，
  选择「继续访问」即可（证书仅用于加密授权凭据，不提供第三方信任链）；
  **未开启授权** 时为明文 HTTP，可直接访问、无安全提示。

### 3. 发现并连接其它设备

1. 点击工具栏 `添加`，弹出「查找」对话框。
2. 输入目标机器名（作为**正则表达式**，留空/`.*` 匹配全部），点击「查找」。
3. 程序持续发送多播查询，下方列表会显示应答的机器（已去重）。
4. 双击列表中的机器，即把它加入地址栏「机器」下拉框并切换过去。也可直接在下
   拉框末项「+ 添加客户端…」触发查找对话框。

### 4. 与远程设备互传文件

在「机器」下拉框选择某台远程设备后：

- **下载**：双击文件（下载并打开）/ 把文件拖到本地视图或桌面。
- **上传**：把本地文件拖入远程视图的目录中（可一次拖入多个）。上传时会弹出进度
  窗口，显示当前文件、整体进度、实时速度，并可随时取消。
- 远程目录为**只读**，不支持在其中新建/删除/粘贴。
- 收发两端都是**分片流式**处理（4 MB/片，边收边写），因此单个文件可以超过 2 GB，
  也不会把整个文件读进内存。

### 5. 开启访问授权

打开顶部的「访问需授权」开关：

- 之后任何未授权设备访问你的共享，你都会收到弹窗，显示请求方的
  **机器名与 IP**，可选择「允许」或「拒绝」。
- 超时（5 分钟）未处理视为拒绝；关闭开关会清空所有在途/已批准授权。
- 对浏览器与桌面客户端均生效，授权过程对终端用户透明。
- 开启授权后共享服务自动切换为 **HTTPS（加密）** 以保护授权凭据；关闭时则
  用明文 **HTTP**，浏览器等访问不再有证书告警。协议随开关自动切换并重启服务。

### 6. 手机扫码访问当前目录

地址栏「机器」下拉框右侧显示本机**当前共享目录**对应的网页二维码：

- 手机（需与本机在同一局域网）扫码，即在浏览器打开该目录页面，浏览 / 下载文件。
- 二维码地址随当前目录与「访问需授权」开关实时刷新：未开启授权为
  `http://<本机IP>:5000/browse/...`（无证书告警），开启授权为
  `https://<本机IP>:5000/browse/...`。
- **点击二维码** 弹出大图预览，可长按/扫码，或用「复制地址」按钮把访问地址
  复制到剪贴板，方便发给他人。
- 进入**远程浏览**（选中其它设备）时，该二维码自动禁用，因为此时展示的是
  对方的目录、而非本机共享。

---

## 协议 / 端口一览

| 用途 | 协议 | 端口 | 说明 |
|------|------|------|------|
| Web 文件服务 | HTTP（未授权）/ HTTPS（需授权） | 5000 | 协议随「访问需授权」开关自动切换 |
| 局域网发现 | UDP 多播 | 5001 | 组播组 `239.255.42.99` |

---

## 目录结构

```
dshare/
├── main.cpp              # 程序入口，初始化 DApplication（DTK）
├── mainwindow.{h,cpp}    # 主窗口：本地浏览、拖拽、跨机传输、授权弹窗
├── fileserver.{h,cpp}    # 内嵌 HTTP/HTTPS 服务（浏览/下载/上传/授权）
├── discovery.{h,cpp}     # 局域网多播设备发现
├── remotemodel.{h,cpp}   # 远程共享目录只读模型（拖拽支持）
├── finddialog.{h,cpp}    # 「查找设备」对话框
├── authmanager.{h,cpp}   # 客户端侧授权握手管理
├── fileuploader.{h,cpp}  # 分片上传器与上传进度窗口
├── updirproxy.h          # 本地目录的「返回上一级」代理模型
├── dshare.desktop        # 桌面启动项
├── translations/         # 中文本地化（zh_CN）
└── CMakeLists.txt        # 构建配置
```

---

## License

本程序以 **GNU General Public License v3 (GPL-3.0)** 发布，详见仓库根目录的
`LICENSE` 文件。

---

# DShare — English

**DShare** is a file-sharing application built with **DTK (Deepin Tool Kit)**
specifically for the **deepin desktop environment**. It shares a local
directory (`~/myshare`) over the LAN: a native DTK GUI for local file
management plus a built-in HTTP/HTTPS server so other devices can browse,
download, and upload files via a browser or another instance of this app.

**Highlights**

- Native DTK6 GUI, consistent with the deepin / UOS v25 desktop look and feel.
- Local management of `~/myshare` (create folder, copy/paste, drag-drop, delete).
- Built-in web server on **port 5000**. When access authorization is **off** it
  serves plain **HTTP** (no self-signed cert warning); when **on** it serves
  **HTTPS** with an auto-generated self-signed cert to protect credentials.
  Browse, download, upload, and create folders from any browser.
- LAN peer discovery via multicast (UDP 5001, group `239.255.42.99`), with
  machine-name regex matching.
- Read-only remote browsing of other peers, with drag-to-download /
  drag-to-upload file transfer.
- Optional **access authorization**: when enabled, every access must be approved
  by the host via a dialog showing the requester's machine name and IP. Enabling
  it switches the web service to HTTPS to protect the authorization credentials.

**Build**

```bash
mkdir build && cd build
cmake .. && make -j"$(nproc)"
./dshare
```

Requires CMake ≥ 3.13, Qt6 (Core/Widgets/Network/HttpServer), DTK6
(Core/Gui/Widget/Log), and `openssl` (runtime, for certificate generation).

Licensed under GPL-3.0.
