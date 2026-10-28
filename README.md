# libmiplay

`libmiplay` 是一个面向 C/C++ 项目的小米妙播接收端库（当前为**最小可验证实现**）。

> 重要说明：当前仓库最初仅包含空 README，且执行环境限制了克隆额外仓库，因此本次实现无法直接读取 `philippe44/libraop`、`FusionPlay-Android`、`MiPlayForWindows`、`miplay-bridge` 的本地源码进行对照。为了避免伪造协议实现，本项目仅提供基于可验证离线测试的接收框架与占位协议解析，并明确列出未确认项。

## 1. 项目范围

已实现：
- 公共 API（创建/启动/停止/销毁生命周期）
- UDP 接收循环与线程模型
- 回调机制（日志、状态、音频帧、错误）
- 最小协议包解析器（握手/音频帧/会话结束三类示例包）
- 命令行测试工具 `miplay-receiver-test`
- 离线协议单元测试

未实现（需参考协议源码或抓包证据补完）：
- 真实 MiPlay 服务发现流程
- 真实握手/鉴权/会话协商字段
- 真实控制命令全集
- 编码格式协商与专用解码链路
- 与真实 Android/Windows MiPlay 发送端互通验证

## 2. 构建

```bash
cmake -S /home/runner/work/libmiplay/libmiplay -B /home/runner/work/libmiplay/libmiplay/build
cmake --build /home/runner/work/libmiplay/libmiplay/build -j
```

可选开关：
- `-DLIBMIPLAY_BUILD_SHARED=ON|OFF`
- `-DLIBMIPLAY_BUILD_STATIC=ON|OFF`
- `-DLIBMIPLAY_BUILD_TOOLS=ON|OFF`
- `-DLIBMIPLAY_BUILD_TESTS=ON|OFF`

## 3. 测试

```bash
ctest --test-dir /home/runner/work/libmiplay/libmiplay/build --output-on-failure
```

## 4. 公共 API 示例

```c
#include <libmiplay/libmiplay.h>

static void on_audio(const uint8_t* data, size_t len, uint64_t pts, void* u) {
  (void)data; (void)len; (void)pts; (void)u;
}

int main(void) {
  libmiplay_config_t cfg = {
    .receiver_name = "example",
    .bind_address = "0.0.0.0",
    .audio_port = 8899,
    .socket_timeout_ms = 200,
  };
  libmiplay_callbacks_t cb = {.on_audio_frame = on_audio};
  libmiplay_receiver_t* r = NULL;
  if (libmiplay_receiver_create(&cfg, &cb, &r) != LIBMIPLAY_OK) return 1;
  libmiplay_receiver_start(r);
  libmiplay_receiver_stop(r);
  libmiplay_receiver_destroy(r);
  return 0;
}
```

## 5. 测试工具

```bash
/home/runner/work/libmiplay/libmiplay/build/miplay-receiver-test --bind 0.0.0.0 --port 8899
/home/runner/work/libmiplay/libmiplay/build/miplay-receiver-test --simulate --dump /tmp/miplay.pcm
```

参数：
- `--bind <ip>` 监听地址
- `--port <port>` 监听端口
- `--dump <file>` 将接收音频写入文件
- `--simulate` 注入离线模拟包（不依赖真实发送端）

## 6. 与 `libraop` 风格对应关系（当前实现级别）

- 保留库化 API + 回调驱动数据流风格
- 保留接收线程 + socket 循环 + 生命周期状态管理
- 保留独立测试工具
- AirPlay 专有语义未复用，改为 MiPlay 命名空间接口

## 7. 迁移说明（从 `libraop` 到 `libmiplay`）

- 上层保持“创建 -> 启动 -> 回调接收 -> 停止 -> 销毁”生命周期即可迁移
- 需将 AirPlay 回调/状态映射到 `libmiplay_callbacks_t`
- 真实 MiPlay 协议字段与控制命令尚未完成，现阶段仅适合框架接入与离线验证

## 8. 已知限制与平台

- 当前首要支持 Linux（pthread + POSIX socket）
- 未提供 Windows/macOS 适配层
- 未集成外部编解码器，音频帧按字节透传给回调

## 9. 无法实现/未确认功能清单（按要求模板）

功能：MiPlay 服务发现（广播/组播/注册）
当前状态：未实现
无法实现或未确认的原因：缺少可验证的本地参考源码与抓包证据
参考依据：本仓库当前无协议实现历史代码
对上层调用者的影响：需手动指定接收地址与端口，无法自动发现
后续实现建议：在可访问参考源码后补齐发现状态机和超时重试

功能：真实握手与会话鉴权
当前状态：仅有离线模拟包类型
无法实现或未确认的原因：真实字段、端口、加密流程未有本地证据
参考依据：同上
对上层调用者的影响：无法与真实发送端保证互通
后续实现建议：基于参考项目与抓包交叉验证后补齐

功能：编码协商与解码
当前状态：音频 payload 透传
无法实现或未确认的原因：未确认实际 codec 与 framing
参考依据：同上
对上层调用者的影响：上层需自行处理解码或仅用于链路测试
后续实现建议：先确认发送端 codec，再引入可选依赖并实现解码路径

## 10. 后续建议

1. 获取并本地化参考仓库代码，对照补齐模块映射表。
2. 基于真实发送端与抓包建立协议一致性测试。
3. 在确认 codec 后补充解码管线与端到端音频验证。
4. 增加断线重连、异常输入和状态恢复测试覆盖。
