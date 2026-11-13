/**
 * Yati preload script.
 *
 * Loaded before the page's own scripts (see webui/index.html) and exposes the
 * framework API on `window.yati`. Every call is a request/response round trip
 * over CEF's message router; window events are pushed from C++ to
 * `window.__yatiDispatch`.
 *
 * Type declarations: ../types/yati.d.ts
 */
(function () {
  'use strict';

  if (window.yati) {
    return; // Already installed (e.g. page reloaded in place).
  }

  // --- Errors ------------------------------------------------------------

  /** Error thrown by every Yati API call that fails. */
  class YatiError extends Error {
    constructor(code, message, channel) {
      super(message || 'Yati API call failed');
      this.name = 'YatiError';
      this.code = code;
      this.channel = channel;
    }
  }

  // --- Transport ---------------------------------------------------------

  let transport = null;

  function call(channel, params) {
    return new Promise(function (resolve, reject) {
      if (typeof window.cefQuery !== 'function') {
        reject(new YatiError(-32008, 'Yati IPC is not available (is this page running under Yati?)', channel));
        return;
      }
      transport.request(channel, params || {}, resolve, reject);
    });
  }

  // Requests are independent: CefMessageRouter correlates each one by id, so
  // a pending call (for example an open native dialog) never blocks others.
  transport = {
    request: function (channel, params, resolve, reject) {
      window.cefQuery({
        request: JSON.stringify({ channel: channel, params: params }),
        persistent: false,
        onSuccess: function (response) {
          let envelope;
          try {
            envelope = JSON.parse(response);
          } catch (error) {
            reject(new YatiError(-32009, 'Malformed IPC response: ' + response, channel));
            return;
          }
          if (envelope.ok === false) {
            const detail = envelope.error || {};
            reject(new YatiError(detail.code, detail.message, channel));
            return;
          }
          resolve(envelope.result);
        },
        onFailure: function (code, message) {
          reject(new YatiError(code, 'IPC transport failed: ' + message, channel));
        },
      });
    },
  };

  // --- Events ------------------------------------------------------------

  const listeners = new Map();

  function on(event, handler) {
    const handlers = listeners.get(event) || [];
    handlers.push(handler);
    listeners.set(event, handlers);
    return function off() {
      off_(event, handler);
    };
  }

  function off_(event, handler) {
    const handlers = listeners.get(event);
    if (!handlers) {
      return;
    }
    const index = handlers.indexOf(handler);
    if (index !== -1) {
      handlers.splice(index, 1);
    }
  }

  function once(event, handler) {
    const unsubscribe = on(event, function (payload) {
      unsubscribe();
      handler(payload);
    });
    return unsubscribe;
  }

  // Called from C++ (src/ipc.cpp Ipc::Emit).
  window.__yatiDispatch = function (envelope) {
    if (!envelope || typeof envelope.event !== 'string') {
      return;
    }
    if (envelope.event === 'window:created') {
      currentWindowId = envelope.payload && envelope.payload.windowId ? envelope.payload.windowId : currentWindowId;
    }
    const handlers = listeners.get(envelope.event) || [];
    for (const handler of handlers.slice()) {
      try {
        handler(envelope.payload);
      } catch (error) {
        console.error('[yati] listener for "' + envelope.event + '" threw:', error);
      }
    }
    const everywhere = listeners.get('*') || [];
    for (const handler of everywhere.slice()) {
      try {
        handler({ event: envelope.event, payload: envelope.payload });
      } catch (error) {
        console.error('[yati] wildcard listener threw:', error);
      }
    }
  };

  let currentWindowId = 0;

  // --- Namespaces --------------------------------------------------------

  const app = {
    info: function () {
      return call('app.info');
    },
    paths: function () {
      return call('app.paths');
    },
    quit: function () {
      return call('app.quit');
    },
    channels: function () {
      return call('app.channels');
    },
    /** Pushes an event to every window (including this one). */
    broadcast: function (event, payload) {
      return call('app.broadcast', { event: event, payload: payload });
    },
  };

  const windowApi = {
    /** State of this window, or of `opts.windowId` when given. */
    getState: function (opts) {
      return call('window.getState', opts || {});
    },
    setBounds: function (bounds) {
      return call('window.setBounds', bounds || {});
    },
    getTitle: function (opts) {
      return call('window.getTitle', opts || {});
    },
    setTitle: function (title, opts) {
      return call('window.setTitle', Object.assign({ title: String(title) }, opts || {}));
    },
    minimize: function (opts) {
      return call('window.minimize', opts || {});
    },
    maximize: function (opts) {
      return call('window.maximize', opts || {});
    },
    restore: function (opts) {
      return call('window.restore', opts || {});
    },
    setFullscreen: function (fullscreen, opts) {
      return call('window.setFullscreen', Object.assign({ fullscreen: !!fullscreen }, opts || {}));
    },
    setAlwaysOnTop: function (onTop, opts) {
      return call('window.setAlwaysOnTop', Object.assign({ onTop: !!onTop }, opts || {}));
    },
    show: function (opts) {
      return call('window.show', opts || {});
    },
    hide: function (opts) {
      return call('window.hide', opts || {});
    },
    activate: function (opts) {
      return call('window.activate', opts || {});
    },
    center: function (opts) {
      return call('window.center', opts || {});
    },
    close: function (opts) {
      return call('window.close', opts || {});
    },
    /** All open windows. */
    list: function () {
      return call('window.list');
    },
    /** Opens a new window; resolves with `{ windowId }`. */
    create: function (options) {
      return call('window.create', options || {});
    },
    /** Window id of the window that owns this page (0 until known). */
    currentId: function () {
      return currentWindowId;
    },
  };

  const fs = {
    readFile: function (path, options) {
      return call('fs.readFile', Object.assign({ path: path }, options || {}));
    },
    /** Convenience: resolves with the UTF-8 content. */
    readText: function (path) {
      return call('fs.readFile', { path: path }).then(function (result) {
        return result.data;
      });
    },
    writeText: function (path, text, options) {
      return call('fs.writeFile', Object.assign({ path: path, data: String(text) }, options || {}));
    },
    readBase64: function (path) {
      return call('fs.readFile', { path: path, encoding: 'base64' }).then(function (result) {
        return result.data;
      });
    },
    writeBase64: function (path, base64) {
      return call('fs.writeFile', { path: path, data: base64, encoding: 'base64' });
    },
    readJson: function (path) {
      return call('fs.readJson', { path: path });
    },
    writeJson: function (path, data, options) {
      return call('fs.writeJson', Object.assign({ path: path, data: data }, options || {}));
    },
    readDir: function (path) {
      return call('fs.readDir', { path: path });
    },
    stat: function (path) {
      return call('fs.stat', { path: path });
    },
    exists: function (path) {
      return call('fs.exists', { path: path });
    },
    mkdir: function (path, recursive) {
      return call('fs.mkdir', { path: path, recursive: recursive !== false });
    },
    remove: function (path, recursive) {
      return call('fs.remove', { path: path, recursive: recursive !== false });
    },
    rename: function (from, to) {
      return call('fs.rename', { from: from, to: to });
    },
    copy: function (from, to, options) {
      return call('fs.copy', Object.assign({ from: from, to: to }, options || {}));
    },
    realpath: function (path) {
      return call('fs.realpath', { path: path });
    },
    tempDir: function () {
      return call('fs.tempDir');
    },
  };

  const dialog = {
    /** Resolves with `{ paths, path, canceled }`. */
    openFile: function (options) {
      return call('dialog.openFile', options || {});
    },
    openFolder: function (options) {
      return call('dialog.openFolder', options || {});
    },
    saveFile: function (options) {
      return call('dialog.saveFile', options || {});
    },
  };

  const shell = {
    openExternal: function (url) {
      return call('shell.openExternal', { url: String(url) });
    },
    openPath: function (path) {
      return call('shell.openPath', { path: String(path) });
    },
    showItemInFolder: function (path) {
      return call('shell.showItemInFolder', { path: String(path) });
    },
    /** Runs a command; resolves with `{ code, stdout, stderr, timedOut }`. */
    exec: function (command, options) {
      const params = Object.assign({}, options || {});
      params.command = command;
      return call('shell.exec', params);
    },
  };

  const clipboard = {
    readText: function () {
      return call('clipboard.readText');
    },
    writeText: function (text) {
      return call('clipboard.writeText', { text: String(text) });
    },
  };

  const notify = {
    send: function (title, message, options) {
      const params = Object.assign({}, options || {});
      params.title = String(title);
      if (message !== undefined) {
        params.message = String(message);
      }
      return call('notify.send', params);
    },
  };

  const session = {
    cookies: {
      /** All cookies, or the cookies matching `url`. */
      get: function (url) {
        return call('session.cookies.get', url ? { url: url } : {});
      },
      set: function (options) {
        return call('session.cookies.set', options || {});
      },
      remove: function (url, name) {
        return call('session.cookies.remove', { url: url, name: name });
      },
      clear: function () {
        return call('session.cookies.clear');
      },
    },
  };

  const devtools = {
    open: function () {
      return call('devtools.open');
    },
    close: function () {
      return call('devtools.close');
    },
    isOpen: function () {
      return call('devtools.isOpen');
    },
  };

  // --- Public surface ----------------------------------------------------

  const api = {
    // 真实版本由 app.info() 在 ready 之后回填，这里不写死版本号。
    version: 'dev',
    YatiError: YatiError,
    invoke: call,
    on: on,
    once: once,
    off: off_,
    /** Resolves once the framework answered; carries `app.info()` data. */
    ready: null,
    app: app,
    window: windowApi,
    fs: fs,
    dialog: dialog,
    shell: shell,
    clipboard: clipboard,
    notify: notify,
    session: session,
    devtools: devtools,
  };

  window.yati = api;

  api.ready = app
    .info()
    .then(function (info) {
      api.version = info.version || api.version;
      return info;
    })
    .catch(function (error) {
      console.error('[yati] initialization failed:', error);
      throw error;
    });
})();
