# RTT scons 板级自动发现架构 — 对标 ChibiOS waf --board=xxx

> **目标**：`scons --board=cuav_v5` 能像 `waf configure --board=CUAVv5` 一样，从 `hwdef/` 目录自动发现硬件定义，不需要维护硬编码字典。
>
> **当前问题**：加新板要改 `SConstruct` + `rtt_bsp_deploy.py` 两套字典，共 ~70 行硬编码。
> ChibiOS 的做法：创建 `hwdef/<board>/hwdef.dat` 目录就自动可用。

---

## Step 0 — 读取全部需要改动的文件，确认现有代码基线

### 涉及文件清单

| 文件 | 角色 | 改动类型 |
|------|------|---------|
| `SConstruct`（根目录） | 入口：解析 `--target`，调 deploy 脚本 | 大改 |
| `Tools/scripts/rtt_bsp_deploy.py` | 部署：复制模板 + 运行 hwdef parser | 大改 |
| `Tools/scripts/rtt_mavgen.py` | MAVLink 头文件生成（被 SConstruct 调） | 确认兼容 |
| `Tools/scripts/rtt_bin_to_apj.py` | 固件打包（被 SConstruct 调） | 确认兼容 |
| `libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py` | hwdef 解析器 | 小改 |
| `libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat` | CUAV V5 硬件定义 | 小改 |
| `libraries/AP_HAL_RTT/hwdef/pixhawk6c_mini/hwdef.dat` | Pixhawk6c mini | 小改 |

### 现有字典内容（全部要删除）

**SConstruct:42-58**（21 行）：
```python
RTT_TARGETS = {
    'cuav_v5': {'board': 'rtt_cuav_v5'},
    'pixhawk6c_mini': {'board': 'rtt_pixhawk6c_mini'},
}

RTT_TARGET_ALIASES = {
    'pixhawk6c_mini': 'pixhawk6c_mini',
    'pixhawk6c-mini': 'pixhawk6c_mini',
    'pixhawk6c mini': 'pixhawk6c_mini',
    'rtt_pixhawk6c_mini': 'pixhawk6c_mini',
    'rtt-pixhawk6c-mini': 'pixhawk6c_mini',
    'cuav_v5': 'cuav_v5',
    'cuav-v5': 'cuav_v5',
    'cuav v5': 'cuav_v5',
    'rtt_cuav_v5': 'cuav_v5',
    'rtt-cuav-v5': 'cuav_v5',
}
```

**rtt_bsp_deploy.py:28-57**（30 行）：
```python
RTT_TARGETS = {
    'cuav_v5': {
        'hwdef': 'libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat',
        'common': 'libraries/AP_HAL_RTT/hwdef/common',
        'msp_src_rel': 'modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/CubeMX_Config/Src/stm32f7xx_hal_msp.c',
        'ports_rel': 'modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/ports',
    },
    'pixhawk6c_mini': {
        'bsp_src_rel': 'libraries/AP_HAL_RTT/rtt_bsp_pixhawk6c_mini',
    },
}

RTT_TARGET_ALIASES = {
    'pixhawk6c_mini': 'pixhawk6c_mini',
    ...（7 行别名）
}
```

---

## Step 1 — 新建 `hwdef/<board>/board_config.py`

### 目标
每个 hwdef 板目录放一个 `board_config.py`，定义该板独有的构建配置。
对标 ChibiOS 的 `chibios_board.mk`，但用 Python。

### 1a. `hwdef/cuav_v5/board_config.py`（新建，30 行）

```python
# hwdef/cuav_v5/board_config.py
# Board-specific build configuration for CUAV V5 (STM32F767).
# Loaded by rtt_bsp_deploy.py when deploying with --board=cuav_v5.


def configure():
    """Return board-specific build config dict.

    Keys recognized by rtt_bsp_deploy.py:
      board_type  — used by mavgen and APJ packaging (from hwdef.h -> BOARD_TYPE)
      packages    — CMSIS/STM32 packages to deploy (from REQUIRED_PACKAGES)
      rtt_bsp_src — legacy BSP source dir (only for boards not yet on hwdef/common)
      msp_src     — path to board-specific HAL MSP init source
      ports_src   — path to board/ports/ overlay directory
    """
    return {
        # APJ board identifier (used by mavgen and uploader)
        # This was previously hardcoded in SConstruct.RTT_TARGETS['cuav_v5']['board']
        'board_type': 'rtt_cuav_v5',

        # CMSIS/STM32 driver packages
        'packages': ['CMSIS-Core-latest', 'stm32f7_cmsis_driver-latest', 'stm32f7_hal_driver-latest'],

        # Board-specific override files (copied on top of common template)
        'msp_src': 'modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/CubeMX_Config/Src/stm32f7xx_hal_msp.c',
        'ports_src': 'modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/ports',
    }
```

### 1b. `hwdef/pixhawk6c_mini/board_config.py`（新建，25 行）

```python
# hwdef/pixhawk6c_mini/board_config.py
# Board-specific build configuration for Pixhawk6c Mini (STM32H743).


def configure():
    return {
        'board_type': 'rtt_pixhawk6c_mini',
        'packages': ['CMSIS-Core-latest', 'stm32h7_cmsis_driver-latest', 'stm32h7_hal_driver-latest'],

        # Legacy: not yet migrated to hwdef/common template
        # Uses full BSP directory copy instead of template + hwdef parser
        'rtt_bsp_src': 'libraries/AP_HAL_RTT/rtt_bsp_pixhawk6c_mini',
    }
```

### 1c. `hwdef/fmuv2/board_config.py`（新建，20 行）

```python
# hwdef/fmuv2/board_config.py
def configure():
    return {
        'board_type': 'rtt_fmuv2',
        'packages': ['CMSIS-Core-latest', 'stm32f4_cmsis_driver-latest', 'stm32f4_hal_driver-latest'],
        'rtt_bsp_src': 'libraries/AP_HAL_RTT/rtt_bsp_fmuv2',
    }
```

---

## Step 2 — 改 `SConstruct`：`--board` 选项 + 自动发现

### 2a. 新增：`discover_boards()` 自动扫描 `hwdef/` 目录

在 `SConstruct` 第 13 行（`AddOption('--target',...)` 之前）插入：

```python
# === Board auto-discovery ===
BOARDS_DIR = 'libraries/AP_HAL_RTT/hwdef'


def discover_boards():
    """Scan hwdef/ directories, return {board_name: {'hwdef': path, 'board_config': path}}."""
    boards = {}
    hwdef_root = os.path.join(Dir('#').abspath, BOARDS_DIR)
    if not os.path.isdir(hwdef_root):
        return boards
    for entry in sorted(os.listdir(hwdef_root)):
        hwdef_file = os.path.join(hwdef_root, entry, 'hwdef.dat')
        if not os.path.isfile(hwdef_file):
            continue
        # Skip entries that start with '_' or are reserved names
        if entry.startswith('_') or entry in ('common', 'scripts'):
            continue
        cfg_file = os.path.join(hwdef_root, entry, 'board_config.py')
        boards[entry] = {
            'hwdef': hwdef_file,
            'board_config': cfg_file if os.path.isfile(cfg_file) else None,
        }
    return boards


def _load_board_config(board_cfg_path):
    """Load board_config.py and return its configure() dict."""
    if not board_cfg_path:
        return {}
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location('board_cfg', board_cfg_path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod.configure()
    except Exception as e:
        print('Warning: failed to load %s: %s' % (board_cfg_path, e), file=sys.stderr)
        return {}
```

### 2b. 新增：`--board` 命令行选项

```python
AddOption('--board',
          dest='board',
          type='string',
          default='',
          help='Board name (e.g. cuav_v5). Auto-discovers hwdef/<board>/ directory.')
```

### 2c. 新增：`_normalize_board()` 别名解析

```python
def _normalize_board(name, available_boards):
    """Normalize board name via alias table, then check availability."""
    if not name:
        return ''
    # Alias map — kept minimal
    ALIAS_MAP = {
        'pixhawk6c-mini': 'pixhawk6c_mini',
        'pixhawk6c mini': 'pixhawk6c_mini',
        'rtt_pixhawk6c_mini': 'pixhawk6c_mini',
        'rtt-pixhawk6c-mini': 'pixhawk6c_mini',
        'cuav-v5': 'cuav_v5',
        'cuav v5': 'cuav_v5',
        'rtt_cuav_v5': 'cuav_v5',
        'rtt-cuav-v5': 'cuav_v5',
        'rtt_fmuv2': 'fmuv2',
        'rtt-fmuv2': 'fmuv2',
        'rtt_fmu_mini': 'rtt_fmu_mini',
        'rtt-fmu-mini': 'rtt_fmu_mini',
    }
    key = name.strip().lower().replace('\\', '/')
    canonical = ALIAS_MAP.get(key, key)
    if canonical in available_boards:
        return canonical
    return ''
```

### 2d. 删除：`RTT_TARGETS`、`RTT_TARGET_ALIASES` 字典（第 42-58 行）

删除前：
```python
RTT_TARGETS = {
    'cuav_v5': {'board': 'rtt_cuav_v5'},
    'pixhawk6c_mini': {'board': 'rtt_pixhawk6c_mini'},
}

RTT_TARGET_ALIASES = {
    ...（16 行）
}
```

删除后：上述字典不存在，由 `discover_boards()` + `_normalize_board()` 替代。

### 2e. 修改主流程（第 146-226 行）：`--target` → `--board`

```python
# 替换原有 --target 解析
target = GetOption('target')
board = GetOption('board')
canonical = ''

if target or board:
    if target:
        # 兼容期：--target 转发到 --board
        print('Warning: --target= is deprecated, use --board=%s' % target, file=sys.stderr)
        canonical = _normalize_target(target)  # 保留旧别名映射不走（旧字典已删）
    elif board:
        canonical = _normalize_board(board, AVAILABLE_BOARDS)
```

### 2f. 修改 mavgen 和 APJ 的 board type 获取方式（第 191, 217 行）

**替换前**（第 191 行）：
```python
board = RTT_TARGETS[canonical_target]['board']
```

**替换后**：
```python
board_cfg = _load_board_config(
    AVAILABLE_BOARDS.get(canonical_target, {}).get('board_config'))
board_type = board_cfg.get('board_type', 'rtt_' + canonical_target)
```

### 改动汇总：`SConstruct`

| 改动 | 位置 | 操作 |
|------|------|------|
| `discover_boards()` | 第 13 行前插入 | +18 行 |
| `_load_board_config()` | 第 13 行前插入 | +14 行 |
| `AddOption('--board')` | 新增 | +4 行 |
| `_normalize_board()` | 第 61 行替换 | +22 行 |
| `RTT_TARGETS` 字典 | 第 42-45 行 | **删除** |
| `RTT_TARGET_ALIASES` 字典 | 第 47-58 行 | **删除** |
| `_normalize_target()` | 第 61-77 行 | 改为调 `_normalize_board()` |
| 主流程 `--target` → `--board` | 第 146-148 行 | +4 行 |
| mavgen APJ 获取 | 第 191, 217 行 | +4 行 |

---

## Step 3 — 改 `rtt_bsp_deploy.py`：文件系统发现替代字典

### 3a. 删除两套字典（第 28-51 行）

删除：
```python
RTT_TARGETS = {...}      # 12 行
RTT_TARGET_ALIASES = {...}  # 8 行
```

### 3b. 新增 `_discover_board()` 函数

```python
def _discover_board(ap_root, board_name):
    """Discover board config from hwdef/<board_name>/ directory.
    
    Returns (hwdef_path, common_dir, board_cfg_dict) or raises error.
    """
    boards_dir = os.path.join(ap_root, 'libraries/AP_HAL_RTT', 'hwdef')
    
    # Paths
    hwdef_path = os.path.join(boards_dir, board_name, 'hwdef.dat')
    common_dir = os.path.join(boards_dir, 'common')
    board_cfg_path = os.path.join(boards_dir, board_name, 'board_config.py')
    
    if not os.path.isfile(hwdef_path):
        return None, "Board '%s': hwdef.dat not found at %s" % (board_name, hwdef_path)
    if not os.path.isdir(common_dir):
        return None, "Common template not found at %s" % common_dir
    return hwdef_path, common_dir, _load_board_config(board_cfg_path)


def _load_board_config(path):
    """Load board_config.py and return configure() dict."""
    if not path or not os.path.isfile(path):
        return {}
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location('board_cfg', path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod.configure()
    except Exception as e:
        print('Warning: board_config.py load failed: %s' % e, file=sys.stderr)
        return {}
```

### 3c. 修改 `deploy()` 函数（第 80-120 行）

**替换前**：
```python
def deploy(ap_root, target):
    canonical = normalize_target(target)
    if canonical not in RTT_TARGETS:
        return None, "Unknown target: %s" % target
    tinfo = RTT_TARGETS[canonical]
    deploy_dir = os.path.join(ap_root, 'build', 'rtt_deploy', canonical)
    ...
    if 'hwdef' in tinfo:
        err = _deploy_hwdef(ap_root, tinfo, deploy_dir)
    else:
        err = _deploy_legacy(ap_root, tinfo, deploy_dir)
```

**替换后**：
```python
def deploy(ap_root, board_name):
    deploy_dir = os.path.join(ap_root, 'build', 'rtt_deploy', board_name)
    
    # 1. Auto-discover board from hwdef/<board_name>/ directory
    hwdef_path, common_dir, board_cfg = _discover_board(ap_root, board_name)
    if not hwdef_path:
        return None, board_cfg  # board_cfg contains error message on failure
    
    # Clean deploy dir
    try:
        if os.path.isdir(deploy_dir):
            shutil.rmtree(deploy_dir)
        os.makedirs(os.path.dirname(deploy_dir), exist_ok=True)
    except Exception as e:
        return None, "Deploy dir cleanup failed: %s" % e
    
    # 2. Dispatch: hwdef mode vs legacy mode
    rtt_bsp_src = board_cfg.get('rtt_bsp_src')
    if rtt_bsp_src:
        # Legacy mode: copy full BSP directory
        err = _deploy_legacy(ap_root, rtt_bsp_src, deploy_dir)
    else:
        # hwdef mode: copy common template + run parser
        err = _deploy_hwdef(ap_root, hwdef_path, common_dir, deploy_dir, board_cfg)
    if err:
        return None, err
    
    _ensure_packages(deploy_dir, board_cfg.get('packages', []))
    return deploy_dir, None
```

### 3d. 修改 `_deploy_hwdef()` 签名

**替换前**：
```python
def _deploy_hwdef(ap_root, tinfo, deploy_dir):
    common_dir = os.path.join(ap_root, _norm(tinfo['common']))
    hwdef_path = os.path.join(ap_root, _norm(tinfo['hwdef']))
```

**替换后**：
```python
def _deploy_hwdef(ap_root, hwdef_path, common_dir, deploy_dir, board_cfg):
```

### 3e. 修改 `_copy_hwdef_board_overrides()` 签名

**替换前**：
```python
def _copy_hwdef_board_overrides(ap_root, tinfo, deploy_dir):
```

**替换后**：
```python
def _copy_hwdef_board_overrides(ap_root, board_cfg, deploy_dir):
    msp_src = board_cfg.get('msp_src')
    if msp_src:
        msp_src_path = os.path.join(ap_root, msp_src)
        ...
    ports_src = board_cfg.get('ports_src')
    if ports_src:
        ports_src_path = os.path.join(ap_root, ports_src)
        ...
```

### 3f. 修改 `_ensure_packages()` 调用

**替换前**（第 248-256 行）：
```python
def _ensure_packages(deploy_dir, canonical):
    pkg_list = REQUIRED_PACKAGES.get(canonical, [])
```

**替换后**：
```python
def _ensure_packages(deploy_dir, pkg_list):
    """Ensure CMSIS/STM32 packages are present in deploy_dir."""
```

并在 `deploy()` 调用处改为：
```python
_ensure_packages(deploy_dir, board_cfg.get('packages', []))
```

**删除**：`REQUIRED_PACKAGES` 字典（第 420-422 行）

---

## Step 4 — 小改 `rtt_hwdef.py`：hwdef.dat 增加 `BOARD_TYPE` define

### 目标
hwdef 解析器生成 `hwdef.h` 时，从 `hwdef.dat` 读取 `BOARD_TYPE` 定义并输出为宏。
这样 `board_type` 信息直接写在 `hwdef.dat` 中，不需要在 `board_config.py` 单独维护。

### 4a. `hwdef/cuav_v5/hwdef.dat` 加一行

```diff
 # CUAV V5 hardware definition for AP_HAL_RTT
 
+define BOARD_TYPE rtt_cuav_v5
+
 # Bus and IMU definitions
 SPIDEV icm20689    SPI1 DEVID1  ICM20689_CS  MODE3  2*MHZ  8*MHZ
```

### 4b. `hwdef/pixhawk6c_mini/hwdef.dat` 加一行

```diff
+define BOARD_TYPE rtt_pixhawk6c_mini
```

### 4c. `rtt_hwdef.py` 解析 `BOARD_TYPE` 并输出到 hwdef.h

在 `rtt_hwdef.py` 的 `write_hwdef_header_content()` 函数中（第 305 行附近）添加：

```python
# Emit BOARD_TYPE macro (used by deploy scripts for mavgen/APJ naming)
board_type = self.get_config('BOARD_TYPE', '')
if board_type:
    f.write('#define BOARD_TYPE "%s"\n\n' % board_type)
```

### 4d. SConstruct 从 hwdef.h 读取 BOARD_TYPE

替换第 191 行：
```python
# 从 hwdef.h 提取 BOARD_TYPE（如果存在）
if not board_type:
    hwdef_h = os.path.join(bsp_deploy_abspath, '_hwdef_gen', 'hwdef.h')
    if os.path.isfile(hwdef_h):
        for line in open(hwdef_h):
            if line.startswith('#define BOARD_TYPE'):
                board_type = line.split('"')[1] if '"' in line else line.split()[-1]
                break
```

### Step 4 改动汇总

| 文件 | 改动 |
|------|------|
| `cuav_v5/hwdef.dat` | +1 行 `define BOARD_TYPE rtt_cuav_v5` |
| `pixhawk6c_mini/hwdef.dat` | +1 行 |
| `rtt_hwdef.py:346` | +4 行（写入 `BOARD_TYPE` 到 hwdef.h） |
| `SConstruct:191` | +6 行（从 hwdef.h 回读） |

---

## Step 5 — 改 `SConstruct` 主流程：整合 `--target` 兼容

### 完整替换主流程（第 146-226 行）

```python
# === Board selection ===
AVAILABLE_BOARDS = discover_boards()

# Support both --board (new) and --target (deprecated)
target = GetOption('target')
board = GetOption('board')
canonical = ''

if target and board:
    print('ERROR: Use --board=xxx only (--target is deprecated)', file=sys.stderr)
    Exit(1)

if target and not board:
    print('Warning: --target= is deprecated, use --board=%s' % target, file=sys.stderr)
    canonical = _normalize_board(target, AVAILABLE_BOARDS)
elif board:
    canonical = _normalize_board(board, AVAILABLE_BOARDS)

if not canonical:
    if target or board:
        print('Unknown board: %s. Available boards:' % (target or board), file=sys.stderr)
        for b in sorted(AVAILABLE_BOARDS.keys()):
            print('  %s' % b, file=sys.stderr)
        Exit(1)
    Exit(0)

# === Build flow ===
if canonical:
    ap_root = Dir('#').abspath
    deploy_script = os.path.join(ap_root, 'Tools', 'scripts', 'rtt_bsp_deploy.py')
    ...
```

### 测试命令集

```bash
# 1) 新 --board 方式
scons --board=cuav_v5

# 2) --target 仍工作（输出 deprecation）
scons --target=cuav_v5

# 3) 别名仍然工作
scons --board=cuav-v5
scons --board=rtt_cuav_v5

# 4) 未知 board 报错
scons --board=nonexistent
# → "Unknown board: nonexistent. Available: cuav_v5, fmuv2, pixhawk6c_mini, rtt_fmu_mini"

# 5) 新 board（仅创建 hwdef 目录就有）
mkdir -p libraries/AP_HAL_RTT/hwdef/my_new_board
cat > libraries/AP_HAL_RTT/hwdef/my_new_board/hwdef.dat << EOF
include ../cuav_v5/hwdef.dat
define BOARD_TYPE rtt_my_new_board
EOF
scons --board=my_new_board
# → 自动发现，无需改任何 Python
```

---

## Step 6 — 验证：确保 `--board=cuav_v5` 编译结果与 `--target=cuav_v5` 一致

### 6a. 双路编译对比

```bash
# 旧方式
scons --target=cuav_v5 2>&1 | tee /tmp/build_old.log

# 新方式（改完后）
scons --board=cuav_v5 2>&1 | tee /tmp/build_new.log

# 对比
diff <(grep -E '(error|warning|rtthread\.bin|Copied|Integrity)' /tmp/build_old.log) \
     <(grep -E '(error|warning|rtthread\.bin|Copied|Integrity)' /tmp/build_new.log)
```

### 6b. 二进制一致性验证

```bash
# 对比生成的 rtthread.bin
md5sum build/rtt_cuav_v5/rtthread.bin
# 两路应该一致
```

### 6c. 烧录验证

```bash
# 烧录后验证行为一致
scons --board=cuav_v5 --upload
```

---

## 总体改动统计

| 文件 | 新增 | 删除 | 净变化 |
|------|------|------|--------|
| `SConstruct` | +68 行 | -40 行 | +28 行 |
| `Tools/scripts/rtt_bsp_deploy.py` | +85 行 | -65 行 | +20 行 |
| `libraries/AP_HAL_RTT/hwdef/scripts/rtt_hwdef.py` | +4 行 | 0 | +4 行 |
| `hwdef/cuav_v5/hwdef.dat` | +1 行 | 0 | +1 行 |
| `hwdef/pixhawk6c_mini/hwdef.dat` | +1 行 | 0 | +1 行 |
| `hwdef/cuav_v5/board_config.py` | +30 行（新建） | 0 | +30 行 |
| `hwdef/pixhawk6c_mini/board_config.py` | +25 行（新建） | 0 | +25 行 |
| `hwdef/fmuv2/board_config.py` | +20 行（新建） | 0 | +20 行 |
| **总计** | **+234 行** | **-105 行** | **+129 行** |

---

## 关键设计决策总结

| 决策 | 选择 | 原因 |
|------|------|------|
| 选项名称 | `--board` | 与 ChibiOS waf 一致，vs `--target` |
| 发现方式 | 文件系统扫描 `hwdef/*/hwdef.dat` | 零硬编码，加板只需创建目录 |
| 板级配置 | `board_config.py` (Python) | scons 原生，无需跨语言桥接 |
| board_type 来源 | `hwdef.dat: define BOARD_TYPE` | hwdef 是板定义主文件，信息集中 |
| 别名表 | 保留但不放大 | 最少别名，`cuav-v5→cuav_v5` 等 |
| 兼容期 | `--target` 继续工作 6 个月 | 不破坏现有工作流 |
| pixhawk6c_mini | `rtt_bsp_src` legacy 模式 | 未迁移到 hwdef/common，保持渐进 |
