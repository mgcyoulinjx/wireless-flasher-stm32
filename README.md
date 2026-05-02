# ESP32 无线 STM32 烧录器

这是一个基于 ESP32-S3 的无线 STM32 固件烧录器。设备会创建 Wi-Fi 热点并提供网页界面，可上传 Intel HEX 固件、在 LittleFS 中保存固件包，并通过 SWD 给 STM32 目标芯片烧录固件。

## 功能特性

- ESP32-S3 Wi-Fi 热点和内置网页界面
- 支持 STM32F1 / F4 / F7 / H7 通过 SWD 烧录
- 默认接线只需要 SWDIO、SWCLK 和 GND，不强制连接 NRST
- 支持上传 Intel HEX，并自动解析地址和校验信息
- 自动转换并保存内部二进制固件包
- 固件包大小和 CRC32 校验
- LittleFS 保存固件包列表
- 重启后保留已选择的待烧录固件
- 网页显示 LittleFS 存储占用和剩余空间
- 网页日志窗口支持一键复制
- 显示烧录进度、状态日志、耗时日志，并支持取消烧录
- 通过 SWD 读取并显示当前 STM32 芯片型号
- TFT 屏显示运行状态、电池电压和充电图标

## 硬件接线

| ESP32-S3 | STM32 |
| --- | --- |
| GND | GND |
| GPIO11 | SWDIO |
| GPIO12 | SWCLK |

默认流程不需要连接 NRST。

## 网页使用流程

1. 给 ESP32-S3 烧录器上电。
2. 连接设备创建的 Wi-Fi 热点。
3. 打开设备网页界面。
4. 上传 Intel HEX 固件文件。
5. 按需把验证通过的固件保存到固件包列表。
6. 在烧录区域选择要烧录的固件。
7. 点击开始烧录，等待擦除、写入和校验完成。

## 固件包格式

网页上传入口使用 Intel HEX 文件。HEX 文件自带地址记录和校验信息，烧录器会自动推断目标 flash 地址，并生成内部使用的二进制固件包。

## 编译

本项目使用 PlatformIO。

```bash
platformio run
```

目标环境：

- 平台：`espressif32 @ 6.5.0`
- 开发板：`exlink_esp32s3_16mb`
- 框架：Arduino
- 文件系统：LittleFS
- 分区表：`my.csv`

## 上传到 ESP32-S3

```bash
platformio run --target upload
```

默认上传波特率为 460800。

## Release 固件烧录偏移

Release 固件包中包含已编译的 ESP32-S3 固件和必要启动文件。可以按以下偏移烧录：

| 偏移地址 | 文件 |
| --- | --- |
| `0x0000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xE000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` |

示例：

```bash
esptool.py --chip esp32s3 --baud 460800 write_flash \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xE000 boot_app0.bin \
  0x10000 firmware.bin
```

## 目录结构

- `src/flash/`：STM32 烧录后端和烧录管理逻辑
- `src/hal/`：SWD 传输和目标芯片控制
- `src/storage/`：LittleFS 固件包存储
- `src/web/`：内置网页服务器、HTML 和 JavaScript
- `src/network/`：Wi-Fi 热点配置
- `src/display/`：TFT 显示状态界面
- `data/`：静态网页界面副本
- `boards/`：自定义 ESP32-S3 开发板定义

## 注意事项

- 默认 SWD 引脚为 GPIO11/GPIO12。
- STM32F1 当前使用整片擦除；STM32F4 / F7 / H7 使用按固件范围擦除扇区。
- 如果目标芯片 flash 后部保存了配置、序列号或校准数据，烧录前请确认所选系列的擦除策略是否符合需求。
