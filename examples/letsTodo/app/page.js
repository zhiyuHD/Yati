'use client';

import { useEffect, useMemo, useState } from 'react';

const STORAGE_KEY = 'letstodo.items.v1';

let nextId = 1;

export default function Page() {
  const [items, setItems] = useState([]);
  const [draft, setDraft] = useState('');
  const [filter, setFilter] = useState('all');
  const [ready, setReady] = useState(false);
  const [runtime, setRuntime] = useState('');

  // 从 localStorage 恢复；app:// 是标准安全源，storage 正常工作。
  useEffect(() => {
    try {
      const raw = window.localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        if (Array.isArray(parsed)) {
          nextId = parsed.reduce((max, item) => Math.max(max, item.id || 0), 0) + 1;
          setItems(parsed);
        }
      }
    } catch {
      /* 存储损坏就当空列表 */
    }
    setReady(true);

    // 运行在 Yati 里时顺手拿到引擎版本；在普通浏览器里打开也不报错。
    if (window.yati) {
      window.yati.app
        .info()
        .then((info) => setRuntime(`${info.engine.name} ${info.engine.version} · CEF ${info.cef.split('+')[0]}`))
        .catch(() => {});
    }
  }, []);

  useEffect(() => {
    if (!ready) return;
    try {
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(items));
    } catch {
      /* 忽略配额错误 */
    }
  }, [items, ready]);

  const add = (event) => {
    event.preventDefault();
    const text = draft.trim();
    if (!text) return;
    setItems((prev) => [{ id: nextId++, text, done: false, at: Date.now() }, ...prev]);
    setDraft('');
  };

  const toggle = (id) =>
    setItems((prev) => prev.map((item) => (item.id === id ? { ...item, done: !item.done } : item)));

  const remove = (id) => setItems((prev) => prev.filter((item) => item.id !== id));

  const clearDone = () => setItems((prev) => prev.filter((item) => !item.done));

  const visible = useMemo(() => {
    if (filter === 'active') return items.filter((item) => !item.done);
    if (filter === 'done') return items.filter((item) => item.done);
    return items;
  }, [items, filter]);

  const remaining = items.filter((item) => !item.done).length;

  return (
    <main className="wrap">
      <h1>LetsTodo</h1>
      <div className="sub">Next.js 静态导出，跑在 Yati 引擎上</div>

      <form className="composer" onSubmit={add}>
        <input
          value={draft}
          onChange={(event) => setDraft(event.target.value)}
          placeholder="要做点什么？回车添加"
          aria-label="新任务"
          autoFocus
        />
        <button type="submit">添加</button>
      </form>

      <div className="filters">
        {[
          ['all', `全部 ${items.length}`],
          ['active', `未完成 ${remaining}`],
          ['done', `已完成 ${items.length - remaining}`],
        ].map(([key, label]) => (
          <button key={key} data-active={filter === key} onClick={() => setFilter(key)}>
            {label}
          </button>
        ))}
      </div>

      {visible.length === 0 ? (
        <div className="empty">
          {filter === 'done' ? '还没有完成的任务' : filter === 'active' ? '没有未完成的任务，休息一下' : '列表是空的，先加一个'}
        </div>
      ) : (
        <ul className="list">
          {visible.map((item) => (
            <li key={item.id} className="item" data-done={item.done}>
              <button
                className="check"
                data-done={item.done}
                onClick={() => toggle(item.id)}
                aria-label={item.done ? '标记为未完成' : '标记为完成'}
              >
                ✓
              </button>
              <span className="text">{item.text}</span>
              <button className="remove" onClick={() => remove(item.id)} aria-label="删除">
                ×
              </button>
            </li>
          ))}
        </ul>
      )}

      <footer className="bar">
        <span>{remaining} 项待办</span>
        {items.some((item) => item.done) && <button onClick={clearDone}>清除已完成</button>}
      </footer>

      <div className="runtime">{runtime ? `Running on ${runtime}` : 'LetsTodo'}</div>
    </main>
  );
}
