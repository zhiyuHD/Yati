#!/usr/bin/env node
/**
 * Headless API check for the Yati runtime.
 *
 * Drives the running app over the DevTools protocol (start it with --inspect),
 * waits for the playground self-test to finish and prints the result. Exits
 * non-zero when any check failed, so it can gate CI or a release build.
 *
 * Usage:  build/bin/yati --no-sandbox --inspect &   then   node scripts/check-api.mjs
 */

const ARG_ENDPOINT = process.argv
  .slice(2)
  .find((arg) => arg.startsWith('--endpoint='))
  ?.slice('--endpoint='.length);

// 端口默认值与 src/options.h 的 kDefaultInspectPort 保持一致；
// 用 --endpoint= 或 YATI_CDP / YATI_INSPECT_PORT 覆盖。
const PORT = process.env.YATI_INSPECT_PORT || '9222';
const ENDPOINT = ARG_ENDPOINT || process.env.YATI_CDP || `http://127.0.0.1:${PORT}`;
const TIMEOUT_MS = Number(process.env.YATI_TIMEOUT_MS || 30000);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function findPage() {
  const response = await fetch(`${ENDPOINT}/json/list`);
  const targets = await response.json();
  return targets.find(
    (target) => target.type === 'page' && target.url.includes('/webui/index.html'),
  );
}

class Cdp {
  constructor(url) {
    this.socket = new WebSocket(url);
    this.nextId = 1;
    this.pending = new Map();
    this.socket.onmessage = (event) => {
      const message = JSON.parse(event.data);
      const entry = this.pending.get(message.id);
      if (!entry) return;
      this.pending.delete(message.id);
      if (message.error) entry.reject(new Error(message.error.message));
      else entry.resolve(message.result);
    };
  }

  ready() {
    return new Promise((resolve, reject) => {
      this.socket.onopen = resolve;
      this.socket.onerror = () => reject(new Error('CDP connection failed'));
    });
  }

  send(method, params) {
    const id = this.nextId++;
    this.socket.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }

  async evaluate(expression) {
    const result = await this.send('Runtime.evaluate', {
      expression,
      returnByValue: true,
      awaitPromise: true,
    });
    if (result.exceptionDetails) {
      throw new Error(result.exceptionDetails.text);
    }
    return result.result.value;
  }
}

const deadline = Date.now() + TIMEOUT_MS;
let target = null;
while (!target && Date.now() < deadline) {
  try {
    target = await findPage();
  } catch (error) {
    // The app is probably still starting; retry until the deadline.
  }
  if (!target) await sleep(500);
}

if (!target) {
  console.error(`✗ no Yati page found at ${ENDPOINT} within ${TIMEOUT_MS} ms`);
  process.exit(1);
}

const cdp = new Cdp(target.webSocketDebuggerUrl);
await cdp.ready();
await cdp.send('Runtime.enable');

let log = '';
while (Date.now() < deadline) {
  log = await cdp.evaluate("document.getElementById('log')?.textContent || ''");
  if (log.includes('自检结束')) break;
  await sleep(400);
}

if (!log.includes('自检结束')) {
  console.error('✗ self-test did not finish in time');
  console.error(log);
  process.exit(1);
}

const passed = (log.match(/✓ /g) || []).length;
const failed = (log.match(/✗ /g) || []).length;

console.log(log.trim());
console.log(`\n${passed} passed, ${failed} failed`);

const runtime = await cdp.evaluate('JSON.stringify(window.yati ? {v: yati.version} : null)');
console.log(`runtime: ${runtime}`);

cdp.socket.close();
process.exit(failed === 0 ? 0 : 1);
