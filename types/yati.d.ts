/**
 * TypeScript declarations for the Yati runtime API.
 *
 * The API is exposed on `window.yati` by `webui/preload.js`, so these types are
 * global: add this file to your project's `include` list (or reference it) and
 * `yati.*` is typed everywhere.
 *
 *   // tsconfig.json
 *   { "include": ["src", "types/yati.d.ts"] }
 */

/** Returned by `yati.on()` / `yati.once()`; calling it removes the listener. */
type YatiUnsubscribe = () => void;

/** Error shape thrown by every failing call. */
interface YatiError extends Error {
  name: 'YatiError';
  /** Numeric error code, see the table in README.md. */
  code: number;
  /** Channel that failed, e.g. "fs.readFile". */
  channel?: string;
}

/** 当前运行的 App（App 包模式下来自 yati.json）。 */
interface YatiAppIdentity {
  /** 唯一标识，缺省取目录名；同时用作缓存目录。 */
  id: string;
  name: string;
  version: string;
  /** true 表示通过 yati <dir> 运行的 App 包，false 表示内置页面。 */
  bundled: boolean;
  /** App 目录（绝对路径）。 */
  path: string;
}

interface YatiAppEngine {
  name: string;
  version: string;
}

interface YatiAppInfo {
  name: string;
  version: string;
  /** Full CEF version, e.g. "156.0.28+g57389f0+chromium-156.0.8078.88". */
  cef: string;
  chromium: number;
  platform: string;
  pid: number;
  arch?: string;
  kernel?: string;
  wayland: boolean;
  /** 当前 App 的身份。 */
  app: YatiAppIdentity;
  /** 引擎自身（版本与 App 版本分开报）。 */
  engine: YatiAppEngine;
}

interface YatiAppPaths {
  home: string;
  /** App 目录；内置页面运行时为空串。 */
  app: string;
  userData: string;
  temp: string;
  cwd: string;
  exe: string;
  resources: string;
}

interface YatiWindowState {
  windowId: number;
  x: number;
  y: number;
  width: number;
  height: number;
  maximized: boolean;
  fullscreen: boolean;
  title: string;
}

interface YatiWindowListEntry extends YatiWindowState {
  browserId: number;
}

interface YatiWindowOptions {
  /** Target another window; defaults to the window hosting the caller. */
  windowId?: number;
}

interface YatiBounds extends YatiWindowOptions {
  x?: number;
  y?: number;
  width?: number;
  height?: number;
}

interface YatiCreateWindowOptions {
  url?: string;
  title?: string;
  width?: number;
  height?: number;
}

interface YatiCreateWindowResult {
  windowId: number;
  url: string;
}

interface YatiStatResult {
  path: string;
  exists: boolean;
  isFile?: boolean;
  isDirectory?: boolean;
  isSymlink?: boolean;
  size?: number;
  mode?: number;
  mtimeMs?: number;
  ctimeMs?: number;
}

interface YatiDirEntry extends YatiStatResult {
  name: string;
}

interface YatiReadDirResult {
  path: string;
  entries: YatiDirEntry[];
}

interface YatiReadFileResult {
  path: string;
  encoding: 'utf8' | 'base64';
  size: number;
  data: string;
}

interface YatiWriteFileResult {
  path: string;
  bytes: number;
}

interface YatiWriteOptions {
  /** Default true: create parent directories. */
  mkdirParents?: boolean;
  /** Default false: truncate before writing. */
  append?: boolean;
}

interface YatiDialogOptions {
  title?: string;
  defaultPath?: string;
  /** Accepted file types, e.g. [".png", ".jpg"]. */
  filters?: string[];
}

interface YatiOpenFileOptions extends YatiDialogOptions {
  multiple?: boolean;
}

interface YatiDialogResult {
  paths: string[];
  /** First selection, or "" when canceled. */
  path: string;
  canceled: boolean;
}

interface YatiExecOptions {
  /** Kill the child after this many milliseconds (default 10000). */
  timeoutMs?: number;
  /** Data written to the child's stdin. */
  stdin?: string;
}

interface YatiExecResult {
  code: number;
  stdout: string;
  stderr: string;
  timedOut: boolean;
  spawnFailed?: boolean;
}

interface YatiCookie {
  name: string;
  value: string;
  domain: string;
  path: string;
  secure: boolean;
  httpOnly: boolean;
  hasExpires: boolean;
  expires?: number;
  expiresMs?: number;
}

interface YatiSetCookieOptions {
  url: string;
  name: string;
  value: string;
  domain?: string;
  path?: string;
  /** Default false: persist for a year. */
  session?: boolean;
}

interface YatiNotifyOptions {
  /** "low" | "normal" | "critical". */
  urgency?: 'low' | 'normal' | 'critical';
  expireMs?: number;
}

/** Payload of the window lifecycle events pushed from C++. */
interface YatiWindowBoundsEvent {
  windowId: number;
  x: number;
  y: number;
  width: number;
  height: number;
  maximized: boolean;
  fullscreen: boolean;
}

interface YatiWindowCreatedEvent {
  windowId: number;
  browserId: number;
  title: string;
  url: string;
}

/** Well-known events. Any string is accepted by `on()`. */
type YatiEventMap = {
  'window:created': YatiWindowCreatedEvent;
  'window:closing': { windowId: number };
  'window:closed': { windowId: number };
  'window:bounds': YatiWindowBoundsEvent;
  'window:focus': { windowId: number };
  'window:blur': { windowId: number };
  'app:ready': { url: string; status: number };
};

interface YatiAppApi {
  info(): Promise<YatiAppInfo>;
  paths(): Promise<YatiAppPaths>;
  quit(): Promise<string>;
  /** Every channel registered on the C++ side. */
  channels(): Promise<string[]>;
  /** Push an event to all windows, including this one. */
  broadcast(event: string, payload?: unknown): Promise<boolean>;
}

interface YatiWindowApi {
  getState(options?: YatiWindowOptions): Promise<YatiWindowState>;
  setBounds(bounds: YatiBounds): Promise<YatiWindowState>;
  getTitle(options?: YatiWindowOptions): Promise<string>;
  setTitle(title: string, options?: YatiWindowOptions): Promise<string>;
  minimize(options?: YatiWindowOptions): Promise<boolean>;
  maximize(options?: YatiWindowOptions): Promise<boolean>;
  restore(options?: YatiWindowOptions): Promise<boolean>;
  setFullscreen(fullscreen: boolean, options?: YatiWindowOptions): Promise<boolean>;
  setAlwaysOnTop(onTop: boolean, options?: YatiWindowOptions): Promise<boolean>;
  show(options?: YatiWindowOptions): Promise<boolean>;
  hide(options?: YatiWindowOptions): Promise<boolean>;
  activate(options?: YatiWindowOptions): Promise<boolean>;
  center(options?: YatiWindowOptions): Promise<boolean>;
  close(options?: YatiWindowOptions): Promise<boolean>;
  list(): Promise<YatiWindowListEntry[]>;
  create(options?: YatiCreateWindowOptions): Promise<YatiCreateWindowResult>;
  /** Window id of the window hosting this page (0 until known). */
  currentId(): number;
}

interface YatiFsApi {
  readFile(path: string, options?: { encoding?: 'utf8' | 'base64' }): Promise<YatiReadFileResult>;
  readText(path: string): Promise<string>;
  writeText(path: string, text: string, options?: YatiWriteOptions): Promise<YatiWriteFileResult>;
  readBase64(path: string): Promise<string>;
  writeBase64(path: string, base64: string): Promise<YatiWriteFileResult>;
  readJson<T = unknown>(path: string): Promise<T>;
  writeJson(path: string, data: unknown, options?: YatiWriteOptions): Promise<YatiWriteFileResult>;
  readDir(path: string): Promise<YatiReadDirResult>;
  stat(path: string): Promise<YatiStatResult>;
  exists(path: string): Promise<boolean>;
  mkdir(path: string, recursive?: boolean): Promise<boolean>;
  /** Resolves with the number of removed entries. */
  remove(path: string, recursive?: boolean): Promise<number>;
  rename(from: string, to: string): Promise<boolean>;
  copy(
    from: string,
    to: string,
    options?: { recursive?: boolean; overwrite?: boolean },
  ): Promise<boolean>;
  realpath(path: string): Promise<string>;
  tempDir(): Promise<string>;
}

interface YatiDialogApi {
  openFile(options?: YatiOpenFileOptions): Promise<YatiDialogResult>;
  openFolder(options?: YatiDialogOptions): Promise<YatiDialogResult>;
  saveFile(options?: YatiDialogOptions): Promise<YatiDialogResult>;
}

interface YatiShellApi {
  openExternal(url: string): Promise<boolean>;
  openPath(path: string): Promise<boolean>;
  showItemInFolder(path: string): Promise<boolean>;
  /**
   * Runs a command. A string runs through `/bin/sh -c`; pass an array to exec a
   * program directly with no shell involved.
   */
  exec(command: string | string[], options?: YatiExecOptions): Promise<YatiExecResult>;
}

interface YatiClipboardApi {
  readText(): Promise<string>;
  writeText(text: string): Promise<boolean>;
}

interface YatiNotifyApi {
  send(title: string, message?: string, options?: YatiNotifyOptions): Promise<boolean>;
}

interface YatiCookieApi {
  get(url?: string): Promise<YatiCookie[]>;
  set(options: YatiSetCookieOptions): Promise<boolean>;
  remove(url: string, name: string): Promise<boolean>;
  clear(): Promise<boolean>;
}

interface YatiSessionApi {
  cookies: YatiCookieApi;
}

interface YatiDevtoolsApi {
  open(): Promise<boolean>;
  close(): Promise<boolean>;
  isOpen(): Promise<boolean>;
}

interface YatiApi {
  /** Runtime version, injected from the repository's VERSION file at build time. */
  version: string;
  /** Error class used by failing calls. */
  YatiError: new (code: number, message: string, channel?: string) => YatiError;
  /** Escape hatch: call any registered channel directly. */
  invoke<T = unknown>(channel: string, params?: Record<string, unknown>): Promise<T>;

  on<K extends keyof YatiEventMap>(event: K, handler: (payload: YatiEventMap[K]) => void): YatiUnsubscribe;
  on(event: string, handler: (payload: any) => void): YatiUnsubscribe;
  once<K extends keyof YatiEventMap>(event: K, handler: (payload: YatiEventMap[K]) => void): YatiUnsubscribe;
  once(event: string, handler: (payload: any) => void): YatiUnsubscribe;
  off(event: string, handler: (payload: any) => void): void;

  /** Resolves with `app.info()` once the runtime answered. */
  ready: Promise<YatiAppInfo>;

  app: YatiAppApi;
  window: YatiWindowApi;
  fs: YatiFsApi;
  dialog: YatiDialogApi;
  shell: YatiShellApi;
  clipboard: YatiClipboardApi;
  notify: YatiNotifyApi;
  session: YatiSessionApi;
  devtools: YatiDevtoolsApi;
}

interface Window {
  yati: YatiApi;
}

declare const yati: YatiApi;
