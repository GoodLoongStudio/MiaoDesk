# MiaoDesk 壁纸与小组件内容包生命周期

## 目标

MiaoDesk 将壁纸和桌面小组件统一视为“可替换内容包”。运行时不依赖用户下载时的原始路径，也不把文件夹名称当作身份；内容包的稳定身份只来自 `manifest.json` 中的 `id`。

核心目标：

- 壁纸与小组件统一使用稳定 Content ID。
- 用户安装后统一进入 MiaoDesk 托管目录，原下载文件可删除。
- 文件夹改名、包在托管目录内重排时，不改变内容身份。
- 同 ID 的用户包支持安全替换/升级。
- 内置包保持只读，用户包不能静默覆盖内置包。
- 导入、移动、替换、升级、卸载最终由统一 API 完成，而不是 UI 自己操作文件。

## 内容包格式

壁纸包：

```text
*.mdwall/
├─ manifest.json
├─ scene.json / index.html
├─ parameters.json      # 可选
├─ preview.*            # 可选
└─ assets/...           # 可选
```

小组件包：

```text
*.mdwidget/
├─ manifest.json
├─ scene.json / index.html
├─ parameters.json      # 可选
├─ preview.*            # 可选
└─ assets/...           # 可选
```

`manifest.json` 至少必须包含：

```json
{
  "schema": 1,
  "id": "com.goodloong.example",
  "name": "Example",
  "version": "1.0.0",
  "kind": "wallpaper",
  "runtime": "scene",
  "entry": "scene.json"
}
```

`kind` 与目录扩展名必须一致：

- `kind=wallpaper` → `.mdwall`
- `kind=widget` → `.mdwidget`

## 稳定身份

统一稳定引用格式：

```text
content:<manifest.id>
```

例如：

```text
content:com.goodloong.glass-clock
content:com.goodloong.weather-glass
content:com.goodloong.miao-cloud
```

以下内容都不是身份：

- 下载路径
- 当前绝对路径
- 文件夹名
- 显示名称 `name`
- 缩略图路径

因此：

```text
GlassClock.mdwidget
```

改名为：

```text
MyClock.mdwidget
```

只要 `manifest.json` 的 `id` 不变，内容身份就不变。

## 目录规则

### 开发仓库

```text
assets/
├─ wallpapers/
│  └─ *.mdwall/
└─ widgets/
   └─ *.mdwidget/
```

### 安装目录中的内置内容

```text
<MiaoDesk安装目录>/
├─ Wallpapers/
│  └─ *.mdwall/
└─ Widgets/
   └─ *.mdwidget/
```

内置目录只读，应用运行时不得修改。

### 用户托管内容

```text
%LOCALAPPDATA%/MiaoDesk/
├─ WallpaperLibrary/
│  ├─ Packages/
│  │  └─ *.mdwall/
│  ├─ Media/
│  ├─ Thumbnails/
│  └─ library.ini
│
└─ DesktopWidgets/
   ├─ Packages/
   │  └─ *.mdwidget/
   └─ Instances/
      └─ <widget-instance-id>.ini
```

用户内容包必须复制到 `Packages` 后再进入正式 Catalog。

## 识别流程

输入可以是用户选择、拖入或下载得到的目录。

```text
选择/拖入 package
    ↓
检查是否为目录
    ↓
检查扩展名 .mdwall / .mdwidget
    ↓
读取 manifest.json
    ↓
校验 schema / id / kind / runtime / entry
    ↓
校验 kind 与目录扩展名一致
    ↓
校验 entry / parameters / preview 均为包内安全路径
    ↓
读取稳定 Content ID
    ↓
得到 content:<id>
```

文件夹名称只用于展示和文件系统组织，不参与身份判断。

## 用户安装流程

用户安装不直接引用源目录，而是进行托管复制：

```text
D:/Downloads/CoolClock.mdwidget
    ↓ Inspect + Validate
manifest.id = com.example.cool-clock
    ↓
复制到同一磁盘上的临时 staging 目录
    ↓
再次 Validate staging
    ↓
检查内置 ID 冲突
    ↓
检查用户已有同 ID package
    ↓
原子替换
    ↓
%LOCALAPPDATA%/MiaoDesk/DesktopWidgets/Packages/
com.example.cool-clock.mdwidget
    ↓
Catalog 重新扫描
    ↓
运行时只保存
content:com.example.cool-clock
```

壁纸同理，目标目录改为：

```text
%LOCALAPPDATA%/MiaoDesk/WallpaperLibrary/Packages/
```

## 同 ID 处理规则

### 与内置包冲突

用户包不能静默覆盖内置包。

当前规则：拒绝安装，并返回明确的 `built-in id conflict`。

后续如果需要“官方包更新覆盖”，必须走独立的签名/版本升级机制，不能复用普通用户安装路径。

### 与用户包冲突

同 `id` 的用户包视为同一内容的替换/升级。

流程：

1. 新包复制到 staging。
2. staging 再次完整校验。
3. 旧包重命名为 backup。
4. staging 重命名为规范目标路径。
5. 成功后删除 backup。
6. 任一步失败则回滚旧包。

规范目标名：

```text
<manifest.id>.mdwall
<manifest.id>.mdwidget
```

这样文件夹名最终也可预测，但仍不是身份来源。

## 移动与改名

### 托管目录内部改名

Catalog 扫描 `.mdwall` / `.mdwidget`，再读取 manifest ID，因此目录名改变不会改变身份。

### 移出托管目录

移出 Catalog 扫描根目录后，内容即视为未安装。

配置中仍然保存 `content:<id>`，因此如果以后重新安装同 ID package，原引用可以重新解析。

### 从外部目录导入

一律复制，不使用外部路径作为正式 source。

这样用户删除下载文件、移动下载目录或拔掉移动硬盘，都不会影响已安装内容。

## Catalog 搜索顺序

### Widget

```text
1. <安装目录>/Widgets
2. %LOCALAPPDATA%/MiaoDesk/DesktopWidgets/Packages
```

### Wallpaper

目标顺序：

```text
1. <安装目录>/Wallpapers
2. %LOCALAPPDATA%/MiaoDesk/WallpaperLibrary/Packages
```

同一个稳定 ID 不允许同时存在两个可生效版本。内置内容优先，并且安装 API 应提前拒绝冲突，避免出现“用户文件存在但永远解析不到”的假安装状态。

## 普通图片/视频壁纸

普通图片、视频不是 Content package，继续进入：

```text
%LOCALAPPDATA%/MiaoDesk/WallpaperLibrary/Media
```

它们属于兼容资源路径，不强行伪装为 `.mdwall`。

长期可以提供“打包为 mdwall”的转换能力，但不属于本阶段。

## 安全要求

安装器必须：

- 拒绝非法 manifest。
- 拒绝 `..` 和越界 entry。
- 拒绝 package 内符号链接、junction 和其他 reparse point。
- 不覆盖内置内容。
- 不覆盖目标位置中无法识别的目录。
- staging 与最终目标位于同一托管根目录，便于 rename 替换和回滚。
- 替换完成前不让 Catalog 看见半复制状态。

后续增加：

- package 总文件数上限。
- package 总大小上限。
- 下载来源与签名信息。
- 官方内容签名和升级通道。

## 卸载

只允许卸载用户托管 package。

内置 package 不允许物理删除，只能在产品层隐藏/禁用。

Widget 卸载前需要检查实例：

```text
DesktopWidgets/Instances/<instance-id>.ini
```

后续 UI 应提示：

- 仅删除 package
- 同时删除所有实例和参数状态

壁纸卸载前要检查是否正在某个显示器使用；如果正在使用，需要先切换到安全默认壁纸。

## 旧系统迁移

当前 Widget 已经基本使用 `content:<id>`。

Wallpaper 仍存在一部分 `library.ini -> Source=<absolute path>` 的旧模型，因此迁移分阶段执行：

### Phase 1 — 统一托管安装 API

- 新增通用 Content package inspect/install/replace API。
- 壁纸和组件共享 manifest 校验规则。
- 新安装 package 不依赖外部路径。

### Phase 2 — 统一解析

- Widget Catalog 改为使用共享 package root/resolve 逻辑。
- Wallpaper Catalog 增加稳定 Content ID resolver。
- 新版 `.mdwall` 的 LibraryItem 不再以绝对 package 路径作为核心引用。

### Phase 3 — 旧壁纸迁移

启动时检测旧 `library.ini`：

```text
Source=<.../*.mdwall/...>
```

如果能读取稳定 package ID，则转换为：

```text
content:<id>
```

普通图片/视频仍保留文件 source。

### Phase 4 — UI

导入入口统一支持：

```text
.mdwall
.mdwidget
```

显示：

- 名称
- 作者
- 版本
- Content ID
- 类型
- 运行时
- 安装位置
- 内置/用户

并提供：

- 安装
- 替换/升级
- 卸载
- 打开所在目录

## 当前实现优先级

第一批代码直接完成：

1. 增加用户 Wallpaper/Widget package root 的统一路径函数。
2. 新增 `MiaoContentPackageManager`。
3. 支持 Inspect。
4. 支持用户托管 Install。
5. 支持同 ID 原子 Replace。
6. 拒绝 Built-in ID 冲突。
7. 拒绝 package 内 reparse point。
8. 增加 SelfTest。
9. Widget Catalog 改用统一 package root。

后续再接 Wallpaper Library 的稳定 Content ID 迁移与 UI。
