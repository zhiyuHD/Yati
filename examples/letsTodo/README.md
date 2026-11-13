# LetsTodo

一个用 **Next.js 16 + React 19** 写的 TODO 应用，作为 App 包跑在 Yati 引擎上。

没有后端、没有本地服务器：`next build` 生成静态文件，Yati 用 `app://` 协议提供它们，
`window.yati` 由引擎注入（页面里没有引 `preload.js`）。

## 跑

```bash
# 已提交构建产物，直接运行
./build/bin/yati examples/letsTodo
```

## 重新构建

```bash
cd examples/letsTodo
pnpm install     # 或 npm i
pnpm build       # 产物到 out/
```

## 结构

```
letsTodo/
├── yati.json          # App 清单：id / name / version / main / window
├── app/               # Next 源码（layout.js、page.js、globals.css）
├── next.config.mjs    # output: 'export'
└── out/               # 静态导出产物（已提交，供直接运行）
```

`yati.json` 里的 `main` 指向 `out/index.html`；Yati 会把 **`main` 所在目录**当作站点根，
所以 Next 产物里的绝对路径 `/_next/static/...` 能正确落在 `out/` 内。

## 用到的东西

- `yati.app.info()`：拿 App 与引擎版本，页脚显示的 `Running on Yati …` 就来自它
- `localStorage`：任务持久化，键 `letstodo.items.v1`；缓存目录按 App id 隔离
  （`~/.cache/top.zhiyuhub.letstodo`）
