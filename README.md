# LOCK2MQTT
米家智能门锁接入homeassistant

![](https://raw.githubusercontent.com/killadm/LOCK2MQTT/master/DOC/images/ha.jpg)



## 特性

- 门锁状态

  - 开门
  - 关门
  - 超时未关
  - 敲门
  - 撬门
  - 门卡住

- 锁舌状态

  - 所有锁舌收回
  - 斜舌弹出
  - 方舌/斜舌弹出
  - 呆舌/斜舌弹出
  - 所有锁舌弹出

- 开锁事件

  - 方法

    - 蓝牙
    - 密码
    - 指纹
    - 应急钥匙
    - 转盘方式
    - NFC
    - 一次性密码
    - 双重验证
    - 人工
    - 自动

  - 动作

    - 室外开门
    - 上提把手锁门
    - 反锁
    - 解除反锁
    - 室内开门
    - 门内上锁
    - 开启童锁
    - 关闭童锁

  - 操作者

- 报警事件

  - 密码多次验证失败
  - 指纹多次验证失败
  - 密码输入超时
  - 撬锁
  - 重置按键按下
  - 有人撬锁或钥匙未拔
  - 钥匙孔异物
  - 钥匙未取出
  - NFC设备多次验证失败
  - 超时未按要求上锁

- 电量事件

- WIFIManager配网

- MQTT输出

- OTA在线升级

## 原理

小白万能遥控器的PCB上存在串口测试点，该串口会不停输出运行日志，当它作为米家智能门锁的蓝牙网关时，串口同样会明文输出门锁发送给网关的消息。
本项目通过ESP8266接收网关的串口日志，提取米家智能门锁相关的消息，经过处理后，序列化为json数据，并通过mqtt协议输出。

## 硬件

- **米家智能门锁** x 1

  ![图片](https://raw.githubusercontent.com/killadm/LOCK2MQTT/master/DOC/images/%E7%B1%B3%E5%AE%B6%E6%99%BA%E8%83%BD%E9%94%81.jpg)

- - **小白万能遥控器 声控版** x 1

  ![图片](https://raw.githubusercontent.com/killadm/LOCK2MQTT/master/DOC/images/%E5%B0%8F%E7%99%BD%E4%B8%87%E8%83%BD%E9%81%A5%E6%8E%A7%E5%99%A8%E5%A3%B0%E6%8E%A7%E7%89%88.jpg)

- **ESP8266** x 1
![图片](https://raw.githubusercontent.com/killadm/LOCK2MQTT/master/DOC/images/esp8266.png)

## 烧录

待更新

## 接线

因网关串口输出非常频繁，为了提高效率，默认使用ESP8266的硬串口。
调试完成后，可以通过网关PCB上的VBUS触点给ESP8266供电。

![图片](https://raw.githubusercontent.com/killadm/LOCK2MQTT/master/DOC/images/%E6%8E%A5%E7%BA%BF%E5%9B%BE.jpg)

## 配网

待更新

## 接入homeassistant

待更新

## 致谢

- [jstormx](https://bbs.hassbian.com/home.php?mod=space&uid=19155) 在 [小米智能门锁状态信息接入hass的非典型方法](https://bbs.hassbian.com/thread-8444-1-1.html)中提出蓝牙网关会串口输出运行数据，并给出智能锁协议的官方定义

## 声明

- 本项目仅供学习交流技术使用，未经本人同意，不可用于任何商业用途。
- 因使用本项目造成的一切不良后果与作者无关。
- 源代码遵循Mozilla Public License, version 2.0 许可协议发布。

