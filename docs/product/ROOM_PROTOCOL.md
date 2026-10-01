# 小钛多人对讲接口与协议

文档版本：V1.1  
文档状态：评审稿  
更新日期：2026-09-08

## 1. 方案结论

多人对讲采用“Web 控制设备、设备接入 Room、服务端保存期望状态”的结构。登录用户在现有设备卡片中点击“多人对讲”，替该设备新建、加入或退出房间；浏览器不建立媒体连接，也不接收设备的 Room Token。

第一版使用统一房间模型：6 位数字房间号、可空的 4 位数字密码、单房最多 100 个在线连接。只要存在有效在线租约，房间持续存在；最后一台设备退出或失联后开始 24 小时空房计时，期间有人重新加入则取消，截止时仍为空才解散。房间安静但设备仍在线不算空房。

## 2. 与现有工程的融合

`tirtc-server-example/thing-connect` 已有以下基础：

- `user-server` 提供用户登录、设备列表和 H5 静态资源。
- `device-server` 提供设备身份和 MQTT Token。
- `internal/mqttc` 支持 MQTT 发布、ACK 和在线查询。
- 正式设备 Topic 为 `device/sn_{device_id}/cmd`、`notify` 和 `ack`。
- Redis 使用 `online:sn_{device_id}` 记录 MQTT 在线状态。
- 设备参考实现已有 TiRTC WHIP 适配能力及统一会话仲裁器。

新增 `room-server` 作为多人对讲领域唯一所有者。H5 文件继续放在 `user-server/static`，网关将 `/v1/room/*` 转发到 `room-server`。

```text
H5（user-server 静态页面）
       │ 用户 JWT + device_id
       ▼
room-server ──内部接口──> user-server（验证设备绑定关系）
     │  │  │
     │  │  ├── MySQL：房间、设备关系、租约、outbox
     │  ├──── Redis：限流和短期状态
     ├──────── MQTT：通知设备同步期望状态
     └──────── 探鸽 Room Token API

设备 ──设备 JWT──> room-server
设备 <── WHIP + JSON-RPC 0x2200 ──> 探鸽 Room
```

如果第一阶段不新增进程，房间领域也必须放在独立 `internal/room` 深模块中，Handler 仅负责鉴权、参数解析和响应转换。推荐独立服务，因为租约清理、空房解散和 outbox 都有独立后台生命周期。

### 2.1 设备媒体能力上报

媒体能力与业务场景相关。实时查看、设备通话和微信 VoIP 使用的 SDK、编解码链路或参数可能不同，因此由 `device-server` 统一接收一份按场景分组的能力文档，再拆分保存为多份“设备 + 场景”记录。多人对讲属于设备通话能力，和设备互呼共用 `call`。各场景允许字段和值重复，业务服务建会话时只读取自己的场景，不能把其他场景的能力当作默认值。

新增 `PUT /v1/device/media-capabilities`。请求使用 `Authorization: Bearer <mqtt_token>`；`device_id` 只能从 Token claim 获取，请求体不接受设备 ID。PUT 全量替换该设备的完整能力文档，同一内容重复上报应幂等，并在一个事务中更新所有场景。

```json
{
  "schema_version": 1,
  "stream": {
    "supported": true,
    "up_audio_mt": "opus",
    "up_video_mt": "h264",
    "down_audio_mts": ["opus", "amr"],
    "down_video_mts": ["mjpeg", "h264"],
    "audio_rate": 16000,
    "audio_channels": 1,
    "camera_rotation": 90
  },
  "call": {
    "supported": true,
    "up_audio_mt": "opus",
    "up_video_mt": "h264",
    "down_audio_mts": ["opus"],
    "down_video_mts": ["h264"],
    "audio_rate": 16000,
    "audio_channels": 1,
    "camera_rotation": 90
  },
  "voip": {
    "supported": true,
    "up_audio_mt": "opus",
    "up_video_mt": "h265",
    "down_audio_mts": ["amr", "alaw"],
    "down_video_mts": ["mjpeg"],
    "audio_rate": 8000,
    "audio_channels": 1,
    "camera_rotation": 90
  }
}
```

字段约束：

| 字段 | 允许值 | 含义 |
| --- | --- | --- |
| `schema_version` | `1` | 能力结构版本 |
| `stream` | object | 实时查看能力 |
| `call` | object | 设备互呼和多人对讲共用的设备通话能力 |
| `voip` | object | 微信 VoIP 能力 |
| `supported` | bool | 设备是否明确支持该场景 |
| `up_audio_mt` | `pcm/alaw/opus/amr/none` | 设备可用于上行的首选音频格式 |
| `up_video_mt` | `h264/h265/mjpeg/none` | 设备上行视频格式 |
| `down_audio_mts` | `pcm/alaw/opus/amr` 的非空去重数组，或 `["none"]` | 设备能接收的下行音频格式，按优先级从高到低排列 |
| `down_video_mts` | `h264/mjpeg` 的非空去重数组，或 `["none"]` | 设备能接收的下行视频格式，按优先级从高到低排列 |
| `audio_rate` | `8000/16000` | 当前首选音频采样率 |
| `audio_channels` | `1/2` | 当前首选声道数 |
| `camera_rotation` | `0/90/180/270` | 上行摄像头画面顺时针旋转角度 |

每个场景对象都使用同一组媒体字段。上行字段表示该场景当前能够发送的首选格式；下行数组表示该场景可接收的格式集合，首项是设备首选。设备可以针对三个场景分别提交相同或不同的值。例如 `stream` 可上行 H.264，`voip` 可上行 H.265，`call` 可以只上行 Opus 音频。设备互呼和多人对讲的能力相同，因此都读取 `call`。

实际会话格式仍以 Token 参数、Room 协商结果和 SDK 帧头为准，发送帧必须与上报/协商格式一致。各业务服务从自身可输出格式与对应 `scene` 的设备能力中取交集，并按设备上报顺序选择；没有交集时拒绝建立对应媒体轨道并返回明确错误。每次会话保存 `scene`、`capability_version`、`selected_down_audio_mt` 和 `selected_down_video_mt`，便于问题定位。

`device-server` 校验场景键、枚举、数组非空、去重、最大长度和请求大小后保存，并返回文档版本以及各场景的 `capability_version` 与 `updated_at`。建议每个下行数组最多 4 项，请求体上限为 4 KiB；未知场景键、非法旋转角度、未知枚举、重复值、`none` 与其他值并存或其他矛盾组合返回 HTTP 400。

省略某个场景表示“该场景能力未上报”；明确不支持时必须传 `{ "supported": false }`。两者语义不同。`supported=false` 时不得同时携带媒体字段。

`device-server` 同时提供仅供内部服务调用的 `GET /internal/v1/device/{device_id}/media-capabilities?scene={scene}`。实时服务读取 `stream`，设备互呼和 `room-server` 都读取 `call`，微信 VoIP 读取 `voip`。未上报对应场景时明确返回 `capability_not_reported`，默认不跨场景回退；确需兼容时必须由服务端配置明确指定回退场景并记录日志。

`GET /v1/user/device/list` 只返回设备卡片需要的状态、AI（人工智能）角色和多人对讲摘要，
不携带完整的音视频格式数据。

用户进入“更多 → 设备信息 → 媒体能力”后，再请求三个场景的详细能力。接口使用
JWT（带签名的登录令牌）校验用户，并确认设备归属。数据库记录是唯一事实源；
旧设备缺少字段时返回“未上报”，不能根据其他字段猜测。

当前工程的 `POST /v1/voip/device/profile` 已在 `voip-server` 保存通话 profile，且下行字段仍是单值。迁移时把旧接口数据只映射到 `voip` 节点，将旧 `down_audio_mt/down_video_mt` 转成单元素数组；不能复制到 `stream` 或 `call`。新接口如需兼容旧消费者，可在内部读取响应中临时返回数组首项作为同名旧字段。设备逐步改调 `device-server` 并按场景上报，消费者切换完成后停止旧表写入。

第一版上行每个方向上报一个当前格式，下行直接使用多值数组。未来上行也支持动态多格式时，再增加 `up_audio_mts/up_video_mts`，并保留单值字段表达默认发送格式。

建议新增通用表 `device_media_capabilities`：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `device_id` | varchar(64) 联合主键 | 设备身份 |
| `scene` | varchar(24) 联合主键 | 业务场景 |
| `schema_version` | int | 请求结构版本 |
| `capability_version` | bigint | 该场景每次有效变更后递增 |
| `profile` | json | 经过校验的该场景能力快照 |
| `firmware_version` | varchar(64) nullable | 便于排查能力变化 |
| `updated_at` | datetime | 最近更新时间 |

## 3. 数据模型

### 3.1 `rooms`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | bigint | 主键 |
| `room_id` | varchar(64) unique | 内部 ID，永不复用 |
| `room_code` | char(6) unique | 用户可见房间号，按字符串处理 |
| `owner_user_id` | bigint | 创建账号 |
| `password_verifier` | varbinary(32) nullable | 无密码时为 NULL |
| `status` | varchar(24) | `waiting_join/active/empty_grace/closed` |
| `participant_limit` | smallint | 固定为 100 |
| `empty_deadline` | datetime nullable | 空房 24 小时截止点 |
| `first_joined_at` | datetime nullable | 首次成功入房 |
| `closed_at` | datetime nullable | 解散时间 |
| `version` | bigint | 并发版本 |
| `created_at/updated_at` | datetime | 审计时间 |

### 3.2 `room_code_registry`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `room_code` | char(6) primary key | 通过唯一冲突完成原子占用 |
| `room_id` | varchar(64) | 当前房间 |
| `state` | varchar(16) | `active/cooldown` |
| `reusable_at` | datetime nullable | 解散 24 小时后可复用 |

使用密码学安全随机数生成候选房间号。不能先查询再插入，必须依靠主键冲突重试。房间号允许 `000001` 等前导零。

### 3.3 `device_room_assignments`

该表保存设备“应该在哪个房间”，用于离线执行和重连恢复。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `device_id` | varchar(64) primary key | 一台设备同时只有一个房间 |
| `room_id` | varchar(64) nullable | 当前目标房间 |
| `owner_user_id` | bigint | 操作设备的账号 |
| `desired_state` | varchar(16) | `joined/left` |
| `assignment_version` | bigint | 每次新建、加入、退出递增 |
| `created_at/updated_at` | datetime | 状态时间 |

Web 退出房间时将 `desired_state` 更新为 `left`，不删除历史行。创建或加入时若设备已有有效房间关系，服务端拒绝请求；退出后才能写入新的 `room_id`。每次实际关系变化都递增版本。

### 3.4 `room_presence_leases`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `room_id/device_id` | 联合唯一 | 当前房间连接 |
| `session_id` | varchar(64) | 每次连接生成的新代次 |
| `assignment_version` | bigint | 对应设备期望状态版本 |
| `state` | varchar(16) | `connecting/active/ended` |
| `lease_expires_at` | datetime | 15 秒续租，45 秒过期 |
| `updated_at` | datetime | 更新时间 |

MQTT 在线和 Room 在线是两个状态。`online:sn_{device_id}` 只说明设备连着 MQTT，不能用于房间人数和 24 小时生命周期。

### 3.5 `room_outbox`

事务内记录 `assignment_changed` 和 `room_closed`。后台 worker 提交后投递 MQTT，按事件 ID 去重并采用有界退避。数据库事务中不得等待 MQTT 或外部 HTTP。

## 4. 房间生命周期

```text
新建房间
  → waiting_join
       └── 首台设备 join_room 成功 → active

active
  └── 最后一个 active/connecting 租约结束或超时
       → empty_grace（empty_deadline = now + 24h）

empty_grace
  ├── 有设备重新加入 → active，清空 empty_deadline
  └── 到期仍无有效租约 → closed
```

创建后一直没有设备成功入房，也从创建时间开始计算 24 小时。空房任务锁定房间行，再次确认没有有效 `connecting/active` 租约后才能关闭。

是否发生 PTT、是否有人讲话、音量大小均不更新 `empty_deadline`。只有有效 Room 连接租约表示房间有人。

关闭房间时：

1. `status=closed`，停止签发新 Token。
2. 对仍指向该房间的设备关系更新为 `left` 并递增版本。
3. 写出 `room_closed` outbox。
4. 房间号进入 24 小时冷却。
5. 内部 `room_id` 永不复用。

24 小时通过动态配置 `room.empty_ttl` 管理，默认值为 24h。

## 5. Web 页面

现有 `user-server/static/devices.html` 的设备卡片从三个操作扩展为四个：

```text
实时 | 联系人 | AI 角色 | 多人对讲
```

点击后进入：

```text
/rooms?device_id={device_id}
```

建议新增文件：

```text
user-server/static/rooms.html
user-server/static/js/rooms.js
```

页面根据设备当前关系渲染：

- 无房间：显示“新建房间”和“加入房间”。
- 等待设备：显示房间号和“等待上线”。
- 设备忙：显示“设备忙，请稍后打开房间页或长按 PTT”。
- 正在连接：显示连接进度。
- 已加入：显示房间号、密码状态、在线人数和“退出房间”。
- 房间关闭：显示已解散并回到新建/加入入口。

页面可以每 3 秒轮询状态；未来使用 SSE/WebSocket 时，仍保留详情接口作为完整快照。

设备列表接口直接返回卡片需要的配置摘要，避免浏览器分别拉取角色列表和房间关系后自行拼接：

```json
{
  "device_id": "TIRZ00000001",
  "ai_role": {
    "role_id": "role_xxx",
    "role_name": "小钛伙伴",
    "state": "configured"
  },
  "room_summary": {
    "state": "not_joined",
    "room_code": null,
    "online_count": 0
  }
}
```

`ai_role.state` 取值为 `configured`、`unset` 或 `unavailable`。`user-server` 按设备当前 `role_id` 解析最新角色名称；角色改名后，下次刷新立即返回新名称。角色已删除或当前用户无权访问时返回 `unavailable`，不能继续返回浏览器缓存的旧名称。

## 6. Web API

所有 Web 接口使用用户 Bearer JWT，服务端从 JWT 取用户 ID，并重新验证 `device_id` 绑定当前用户。

### 6.1 查询设备当前房间

```http
GET /v1/room/web/device/{device_id}
```

返回设备在线状态、期望状态、执行状态、房间号、密码模式、在线人数和版本。

### 6.2 替设备新建房间

```http
POST /v1/room/web/device/{device_id}/create
```

```json
{"password":"0573"}
```

无密码传空字符串。服务端在一个事务中创建房间、占用房间号、将设备关系写为 `joined` 并写 outbox。接口不向 Web 返回 Room Token。

### 6.3 替设备加入房间

```http
POST /v1/room/web/device/{device_id}/join
```

```json
{"room_code":"028615","password":"0573"}
```

服务端校验房间状态、密码、用户与设备关系和容量预期。设备原来在其他房间时，原子更新到新房间；实际媒体切换由设备串行执行。

### 6.4 替设备退出房间

```http
POST /v1/room/web/device/{device_id}/leave
```

请求可以携带当前 `room_id/assignment_version` 防止旧页面退出新房间。事务中更新为 `left` 并写 outbox。

## 7. 设备 API

设备接口使用设备 Bearer Token，目标设备 ID 只能来自 Token，不能由请求体指定。

### 7.1 查询期望状态

```http
GET /v1/room/device/assignment
```

```json
{
  "code":200,
  "data":{
    "desired_state":"joined",
    "room_id":"xiaotai_room_01JXYZ",
    "room_code":"028615",
    "assignment_version":14
  }
}
```

密码和 Room Token不在该响应中返回。

### 7.2 获取连接 Token

```http
POST /v1/room/device/connect-token
```

```json
{"room_id":"xiaotai_room_01JXYZ","assignment_version":14,"session_id":"uuid"}
```

服务端再次验证设备期望关系、房间未关闭和 100 人容量，预占 `connecting` 租约，再使用服务端 AppId/AppSecret 调用探鸽 Room Token API。只向该设备返回短期 `peer_id/token/expires_at`。

### 7.3 上报连接状态和续租

```http
POST /v1/room/device/presence
```

```json
{
  "room_id":"xiaotai_room_01JXYZ",
  "session_id":"uuid",
  "assignment_version":14,
  "state":"joined"
}
```

允许 `connecting/joined/suspended/left/connect_failed`。`joined` 创建 45 秒租约，设备每 15 秒续租。旧 `session_id` 或旧版本的上报不能覆盖新状态。

## 8. MQTT 协议

数据库中的设备期望状态是事实源，MQTT 只用于及时唤醒设备同步。通知中不携带 Room Token。

### 8.1 房间关系变化

Topic：`device/sn_{device_id}/cmd`，QoS 1，需要 ACK。

```json
{
  "type":"room_assignment_changed",
  "request_id":"uuid",
  "assignment_version":14,
  "expires_at":1788800000
}
```

设备收到后 ACK，然后调用期望状态接口。重复或乱序消息按更大的 `assignment_version` 处理。

### 8.2 房间关闭

Topic：`device/sn_{device_id}/notify`，QoS 1。

```json
{
  "type":"room_closed",
  "room_id":"xiaotai_room_01JXYZ",
  "assignment_version":15
}
```

设备不通过启动、MQTT 重连、Token 刷新、音频焦点释放或周期定时器兜底查询 assignment。查询入口限于：收到 `room_assignment_changed`；`touch-full` 用户打开“多人对讲”页；`display-key` 或 `headless-key` 在未连接时长按 PTT。离线期间错过通知由下一次用户显式入口恢复，不要求平台在订阅后重投状态。已连接房间后的 presence 租约心跳继续按 `heartbeat_seconds` 执行。

## 9. Room 信令

设备拿到短期凭证后：

```text
TiRtcWhipConnect(peer_id, token)
  → 连接就绪
  → 10 秒内以命令 0x2200 发送首个带 id 的 join_room Request
  → room_snapshot 初始化成员列表
  → participant_joined/left/mic_state_changed 增量更新
  → 退出时发送不带 id 的 leave_room Notification
  → 关闭连接并释放资源
```

默认 `mic_state=off`。本机 PTT 按下后开启采集和上行并同步 `speaking`，松开后关闭上行并同步 `off`。多人可以同时为 `speaking`，不实现麦权接口。

## 10. 设备会话仲裁

新增 `ROOM` 会话类型和局部状态：

```text
IDLE
  → SYNCING
  → WAITING_IDLE
  → FETCHING_TOKEN
  → CONNECTING
  → JOINING
  → JOINED
  → LEAVING
  → IDLE
```

设备当前关系为 `joined`，但 AI、CALL 或 VOIP 占用音频时，上报 `suspended`。业务结束后重新查询关系并获取新 Token 恢复房间。用户通过 Web 退出会把期望状态改为 `left`，因此不会自动恢复。

每次连接生成新的 `session_id` 和 `SessionArbiter generation`。SDK 回调、HTTP 结果、MQTT 和定时器事件都必须核对当前代次，避免旧连接关闭新房间。

## 11. 容量和并发

- 获取 Token 前在事务中预占一个 `connecting` 租约。
- 房间容量统计有效 `connecting + active` 租约，达到 100 后拒绝新连接。
- 同一设备同一时刻只能有一个有效房间租约。
- Token 获取或连接失败时结束预占；超时任务也会清理。
- 设备关系使用设备唯一行、行锁和 `assignment_version` 串行更新。
- 创建或加入时发现设备已有其他有效房间关系，直接拒绝并要求先退出。
- 房间关闭和 Token 签发锁定同一房间记录。
- 设备退出当前房间并结束旧连接后，才能创建或加入其他房间。

## 12. 密码与安全

- 房间号和密码只接受 ASCII 数字并按字符串处理。
- 密码为空时 `password_verifier=NULL`。
- 4 位密码使用服务端 pepper 参与的 HMAC-SHA256，输入包含 `room_id`，不存明文。
- 同一设备连续输错 5 次锁定 10 分钟。
- 加入接口按用户、设备、IP 和房间号限流，防止遍历 6 位空间。
- Web 永远不接收目标设备的 Room Token。
- 设备不保存 AppSecret，服务端每次验证关系后签发短期 Token。
- AppSecret、pepper、Room Token、设备密钥、明文密码和完整鉴权头不进入日志。

## 13. 后台任务

`room-server` 持有以下可取消 worker：

- `lease-expirer`：处理 45 秒过期租约，必要时启动 24 小时空房计时。
- `empty-room-closer`：关闭到期仍为空的房间。
- `code-recycler`：处理房间号冷却记录。
- `outbox-dispatcher`：可靠发送 MQTT 通知。

服务重启后全部任务从数据库时间字段恢复，不能依赖内存定时器。readiness 应覆盖 MySQL、Redis、MQTT 和签发 Token 所需配置。

## 14. H5 原型调整

原型需要展示完整 Web 路径：

1. 设备卡片包含“实时、联系人、AI 角色、多人对讲”，显示设备 ID、当前 AI 角色名称和多人对讲状态摘要；型号、硬件标签、编解码格式和旋转角度通过右上角“更多 → 设备信息”查看。
2. 右上角“•••”先显示与现有小程序一致的“修改设备名称、设备信息、解绑设备、取消”操作面板。
3. 点击多人对讲进入所选设备的页面。
4. 未加入状态提供“新建房间”和“加入房间”。
5. 新建可选择空密码或 4 位密码。
6. 加入输入 6 位房间号和按需出现的密码输入。
7. 已加入状态显示房间号、设备状态、在线人数和“退出房间”。
8. 生命周期文案显示“房间连续无人在线 24 小时后自动解散”。

## 15. 配置与迁移

新增配置：

```text
room.participant_limit = 100
room.empty_ttl = 24h
room.presence_heartbeat = 15s
room.presence_lease = 45s
room.code_cooldown = 24h
room.command_ttl = 20s
room.token_timeout = 5s
```

Schema 改动同时进入有序迁移和 `thing-connect/scripts/schema.sql`。新增服务同步更新 Nginx、Supervisor、服务发现、配置示例和健康检查。密钥只通过部署密钥注入。

## 16. 测试与验收

### 服务端

- `stream/call/voip` 三个场景可以保存不同媒体能力；设备互呼和多人对讲都只读取 `call`，缺少场景时不会静默借用其他场景。
- 下行多格式数组按优先级保存并参与协商；非法格式、重复值、`none` 混用或非 `0/90/180/270` 旋转角度被拒绝。
- 6 位房间号并发唯一、前导零和冷却复用。
- 空密码、4 位密码、错误锁定和限流。
- Web 用户越权和跨账号设备拒绝。
- 重复加入当前房间和重复退出不改变关系；已有房间时重复创建或加入其他房间会被拒绝。
- 100/101 容量边界。
- 房间关闭与新连接并发。
- 在线租约持续超过 24 小时但无人讲话时房间不关闭。
- 最后一个租约失效后 24 小时关闭，期间重连取消关闭。
- 服务重启后租约、截止时间和 outbox 恢复。

### 设备

- 在线设备收到通知、ACK 并同步期望状态。
- 离线设备上线后自动执行新建/加入/退出结果。
- 重复和乱序通知不产生双连接。
- AI/CALL/VOIP 暂停房间后能够恢复。
- Web 已退出时业务结束后不会错误恢复旧房间。
- 多设备同时 PTT 均可上行。
- 旧 TiRTC 回调不能终止新 generation。

### Web

- 四个设备功能入口正确显示。
- 设备卡片只显示关键状态；完整媒体能力可从“更多 → 设备信息”按场景查看。
- 新建、加入、状态、退出完整可用。
- 密码输入和错误状态清晰。
- 页面刷新后从服务端恢复真实状态。
- 移动端宽度和登录过期处理正确。

## 17. 实施顺序

1. 建立房间领域、Schema、生命周期和并发测试。
2. 完成 Web 新建、加入、查询和退出 API。
3. 完成设备期望状态、Token、presence 和 MQTT outbox。
4. 调整 `devices.html`，增加多人对讲入口和房间页面。
5. 在 Linux C/Python 参考实现接入 Room 状态机与 PTT。
6. 完成探鸽 WHIP 和 JSON-RPC 0x2200 联调。
7. 移植小钛固件并验证 AI/通话切换。
8. 进行 100 台、弱网、24/72 小时和灰度测试。
