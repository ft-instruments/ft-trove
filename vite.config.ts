import { defineConfig, type Plugin } from 'vite';
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { dirname, extname, join, resolve } from 'node:path';

// Dev only: `?panel=/path/to/Preset.dspreset` shows that preset's own panel,
// parsed by the real C++ reader (trove-render --ui) with its assets served from
// beside the preset, the way the plugin does it.
function panelPreview(): Plugin {
  const render = resolve('plugin/build/TroveRender_artefacts/Release/TroveRender');
  let dir = '';
  const types: Record<string, string> = {
    '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.gif': 'image/gif', '.svg': 'image/svg+xml',
  };
  // Samples libraries are made on Windows: match path segments case-insensitively.
  const find = (root: string, rel: string): string | null => {
    let at = root;
    for (const part of rel.split('/').filter(Boolean)) {
      if (!existsSync(at)) return null;
      const hit = readdirSync(at).find((n) => n.toLowerCase() === part.toLowerCase());
      if (!hit) return null;
      at = join(at, hit);
    }
    return at;
  };
  return {
    name: 'trove-panel-preview',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const url = new URL(req.url ?? '/', 'http://x');
        if (url.pathname === '/__panel') {
          const preset = url.searchParams.get('preset') ?? '';
          dir = dirname(preset);
          try {
            const json = execFileSync(render, ['--ui', preset], { maxBuffer: 1 << 26 }).toString();
            res.setHeader('content-type', 'application/json');
            res.end(json);
          } catch (e) {
            res.statusCode = 500;
            res.end(String(e));
          }
          return;
        }
        if (url.pathname.startsWith('/ui/') && dir) {
          const rel = decodeURIComponent(url.pathname.slice(4));
          let at = dir;
          for (let up = 0; up < 3; up++) {
            const file = find(at, rel);
            if (file && existsSync(file)) {
              res.setHeader('content-type', types[extname(file).toLowerCase()] ?? 'application/octet-stream');
              res.end(readFileSync(file));
              return;
            }
            at = dirname(at);
          }
          res.statusCode = 404;
          res.end();
          return;
        }
        next();
      });
    },
  };
}

// The plugin's WebView must not depend on how it guesses the text encoding
// (Linux WebKit showed "Â·" for "·"): ship the built UI as pure ASCII.
const asciiHtml = (): Plugin => ({
  name: 'ascii-html',
  transformIndexHtml: { order: 'post', handler: (html) =>
    html.replace(/[^\x00-\x7f]/gu, (c) => `&#x${c.codePointAt(0)!.toString(16)};`) },
});

export default defineConfig({
  base: './',
  plugins: [panelPreview(), asciiHtml()],
  esbuild: { charset: 'ascii' },
  build: { target: 'es2022', assetsInlineLimit: 0 },
});
