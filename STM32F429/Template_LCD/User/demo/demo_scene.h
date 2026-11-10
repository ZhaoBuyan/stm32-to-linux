/**
 ******************************************************************************
 * @file    demo_scene.h
 * @brief   模板工程自带的演示画面 —— 遍历 gfx 的每个原语
 *
 * 关键点：这个文件**不依赖任何具体项目的业务**，只用 gfx。
 *   真板子：main.c 每 20ms 调一次 Demo_Draw()
 *   PC 预览：preview_adapter.c 每帧调一次 Demo_Draw()
 * 同一份绘制代码，两种运行方式。
 ******************************************************************************
 */
#ifndef __DEMO_SCENE_H
#define __DEMO_SCENE_H

#include <stdint.h>

/** 画一帧演示画面
 *  @param now_ms  当前毫秒数（用来让小球动起来）
 *  @param key1    KEY1 是否按下（0/1）
 *  @param key2    KEY2 是否按下（0/1）
 */
void Demo_Draw(uint32_t now_ms, int key1, int key2);

#endif /* __DEMO_SCENE_H */
