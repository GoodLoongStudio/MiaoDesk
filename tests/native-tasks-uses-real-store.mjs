import fs from "node:fs";
import assert from "node:assert/strict";

// D-2:原生待办卡片必须显示**真实**待办。
//
// 缺陷本体:PaintTodayTasks 在 painter 里手写三条待办 ——
// {完成产品设计方案, false}、{与团队同步项目进度, false}、{回复客户邮件, true},
// 外加写死的 L"3" 与 "1 / 3 完成"。它从不读 TodayTaskStore,于是用户在管理界面编辑的
// 待办从不出现在常驻桌面的卡片上,而卡片看起来完全正常。
//
// 一个常驻桌面的组件显示不存在的数据,比显示错误更糟:错误会促使用户去修,
// 假数据只会让人以为自己的待办已经同步好了。
//
// 为什么是源码闸门:NativeWidgetPainter.h 与 NativeWidgetHost.cpp 都要 D2D 头,
// macOS 上编译不了,于是这个调用点此前没有任何本地门覆盖(和 D-1 同一个形状)。
// 纯逻辑那一半 —— BuildTodayTaskCardModel 的形状与"没有数据就说没有数据" —— 由
// src/tests/TodayTaskPresentationTest.cpp 真跑。
//
// 这里只钉三个可读的事实:
//   1. PaintTodayTasks 的每一行、每一个数字都来自 ctx.tasks,没有自己造的数据;
//   2. 宿主在 TodayTasks preset 下真的调 TodayTaskStore::Load,并把模型交给 painter;
//   3. painter 里不再有"自己声明一个任务数组"的写法 —— 那是下一次写死的入口。

const read = (path) => fs.readFileSync(path, "utf8");
const painter = read(process.env.NATIVE_WIDGET_PAINTER_H ?? "src/include/miaodesk/NativeWidgetPainter.h");
const host = read(process.env.NATIVE_WIDGET_HOST_CPP ?? "src/desktop/widgets/NativeWidgetHost.cpp");

function bodyOf(source, signature) {
  const start = source.indexOf(signature);
  assert.notStrictEqual(start, -1, `${signature} 必须存在`);
  let depth = 0;
  for (let i = source.indexOf("{", start); i < source.length; i += 1) {
    if (source[i] === "{") depth += 1;
    else if (source[i] === "}") {
      depth -= 1;
      if (depth === 0) return source.slice(start, i + 1);
    }
  }
  throw new Error(`${signature} 的函数体没有闭合`);
}

const paint = bodyOf(painter, "void PaintTodayTasks(const NativeWidgetPaintContext& ctx)");

// 1. 数据来自上下文里的真实模型。
assert.ok(
  paint.includes("ctx.tasks"),
  "PaintTodayTasks 必须读 ctx.tasks —— 自己造数据就是下一次假数据。",
);
assert.ok(
  /ctx\.tasks->valid/.test(paint),
  "必须先判断快照可用,再显示数字;读不到就说明读不到。",
);

// 原文里那两条写死的输出各有一个判据:数量来自 total,进度来自 progress。
assert.ok(
  /ctx\.tasks->total/.test(paint) && /ctx\.tasks->completed/.test(paint),
  "待办数量与完成数必须来自快照,不能是字面量。",
);
assert.ok(
  /ctx\.tasks->progress/.test(paint),
  "进度条填充必须来自快照 progress(旧的写死 1/3 会让任何真实进度显示成三分之一)。",
);

// 2. 行来自快照,而不是本地数组。
assert.ok(
  !/TaskRow\s+\w+\s*\[\s*\]/.test(paint),
  "PaintTodayTasks 不得再自己声明一个任务数组 —— 那正是旧实现的样子。",
);
assert.ok(
  /ctx\.tasks->rows/.test(paint),
  "行必须来自快照的 rows。",
);
for (const literal of ["完成产品设计方案", "与团队同步项目进度", "回复客户邮件", '"3"', "1 / 3 完成"]) {
  assert.ok(
    !paint.includes(literal),
    `painter 里不得再出现写死的 ${literal}。`,
  );
}

// 3. 宿主真的去读存储,并把模型交给 painter。
const slot = bodyOf(host, "void PaintSlot(NativeSlot& slot)");
assert.ok(
  /TodayTaskStore::Load\(/.test(slot),
  "宿主必须真的读 TodayTaskStore —— 只有 painter 改了不够,数据得有人送来。",
);
assert.ok(
  /BuildTodayTaskCardModel\(/.test(slot),
  "宿主用共享的行模型构造器生成卡片数据。",
);
assert.ok(
  /context\.tasks\s*=\s*&/.test(slot),
  "宿主把模型地址交给 painter 的上下文。",
);

console.log("原生待办卡片:数据来自真实存储,painter 不自己造");
