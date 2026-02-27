(() => {
  // ---------------- Utilities ----------------
  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  // CSS.escape fallback (older browsers)
  const cssEscape = (s) => (window.CSS && CSS.escape) ? CSS.escape(s)
    : String(s).replace(/[^a-zA-Z0-9_-]/g, '\\$&');

  const showToast = (msg) => {
    const t = $('#toast');
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), 2800);
  };

  const setNested = (obj, path, value) => {
    const keys = path.split('.');
    let cur = obj;
    keys.forEach((k, i) => {
      if (i === keys.length - 1) cur[k] = value;
      else { if (!(k in cur)) cur[k] = {}; cur = cur[k]; }
    });
  };

  const getNested = (obj, path) => {
    const keys = path.split('.');
    let cur = obj;
    for (const k of keys) {
      if (cur == null || !(k in cur)) return undefined;
      cur = cur[k];
    }
    return cur;
  };

  const debounce = (fn, ms = 150) => {
    let t;
    return (...args) => { clearTimeout(t); t = setTimeout(() => fn.apply(null, args), ms); };
  };

  // ---------------- Validators ----------------
  const reMac = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/;
  const reIPv4 = /^(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$/;

  const validators = {
    mac: (v) => reMac.test(v) ? null : "Invalid MAC (AA:BB:CC:DD:EE:FF)",
    port: (v) => {
      const n = Number(v);
      if (v === '' || v == null) return "This field is required";
      if (!Number.isInteger(n) || n < 1 || n > 65535) return "Port must be 1–65535";
      return null;
    },
    ip_port: (v) => {
      const [ip, port] = String(v).split(':');
      if (!reIPv4.test(ip || '')) return "Invalid IPv4 address";
      const n = Number(port);
      if (!port || !Number.isInteger(n) || n < 1 || n > 65535) return "Port must be 1–65535";
      return null;
    },
    ip: (v) => {
      const ip = String(v);
      if (!reIPv4.test(ip || '')) return "Invalid IPv4 address";
      return null;
    },
    url: (v) => {
      try {
        const u = new URL(v);
        if (!/^https?:$/.test(u.protocol)) return "Use http or https";
        return null;
      } catch (e) {
        return "Invalid URL";
      }
    }
  };

  function validateAgainstDefinition(def, value) {
    // Required (with special-case for checkboxes)
    if (def.required) {
      const empty = value === undefined || value === null || value === '';
      if (def.type === 'checkbox' || def.type === 'switch') {
        if (value !== true) return "This field is required";
      } else if (empty) {
        return "This field is required";
      }
    }

    // Empty optional -> no further validation
    if (value === undefined || value === null || value === '') {
      return null;
    }

    // Built-in validator
    const t = def.validator;
    if (t && validators[t]) {
      const msg = validators[t](value, def);   // <-- correct invocation
      if (msg) return msg;
    }

    // Regex pattern
    if (def.pattern) {
      try {
        const re = new RegExp(def.pattern);
        if (!re.test(String(value))) return "Invalid format";
      } catch (e) { /* ignore invalid pattern */ }
    }

    // Numeric / string constraints
    if (def.type === 'number') {
      const n = Number(value);
      if (!Number.isFinite(n)) return "Must be a number";
      if (def.min !== undefined && n < def.min) return `Min ${def.min}`;
      if (def.max !== undefined && n > def.max) return `Max ${def.max}`;
    } else if (typeof value === 'string') {
      if (def.minlength !== undefined && value.length < def.minlength) return `Min length ${def.minlength}`;
      if (def.maxlength !== undefined && value.length > def.maxlength) return `Max length ${def.maxlength}`;
    }
    return null;
  }

  function el(tag, attrs = {}, children = []) {
    const e = document.createElement(tag);
    Object.entries(attrs).forEach(([k, v]) => {
      if (k === 'class') e.className = v;
      else if (k === 'for') e.htmlFor = v;
      else if (k === 'html') e.innerHTML = v;
      else e.setAttribute(k, v);
    });
    (Array.isArray(children) ? children : [children]).filter(Boolean).forEach(c => {
      if (typeof c === 'string') e.appendChild(document.createTextNode(c));
      else e.appendChild(c);
    });
    return e;
  }

  function validateField(def, input = null, valueOverride = undefined) {
    const selector = `[name="${cssEscape(def.name)}"]`;
    const elInput = input || document.querySelector(selector);
    if (!elInput) return true;

    let val;
    if (valueOverride !== undefined) val = valueOverride;
    else if (def.type === 'checkbox' || def.type === 'switch') val = elInput.checked;
    else val = elInput.value;

    const msg = validateAgainstDefinition(def, val);
    const id = 'fld_' + def.name.replace(/[^a-zA-Z0-9_]/g, '_');
    const err = document.getElementById(id + '_err');

    if (msg) {
      if (err) { err.textContent = msg; err.classList.add('show'); }
      elInput.classList.remove('valid'); elInput.classList.add('invalid');
      if (elInput.setCustomValidity) elInput.setCustomValidity(msg);
      return false;
    } else {
      if (err) { err.textContent = ''; err.classList.remove('show'); }
      elInput.classList.remove('invalid'); elInput.classList.add('valid');
      if (elInput.setCustomValidity) elInput.setCustomValidity('');
      return true;
    }
  }

  function validateAll(schema) {
    let ok = true;
    (schema.pages || []).forEach(pg => {
      (pg.sections || []).forEach(sec => {
        (sec.fields || []).forEach(def => {
          const res = validateField(def);
          if (!res) ok = false;
        });
      });
    });
    return ok;
  }

  function createField(def) {
    const wrap = el('div', { class: 'field' });
    const id = 'fld_' + def.name.replace(/[^a-zA-Z0-9_]/g, '_');

    const label = el('label', { for: id }, def.label || def.name);
    let input;
    const common = { id, name: def.name, placeholder: def.placeholder || '' };

    switch (def.type) {
      case 'textarea':
        input = el('textarea', common);
        if (def.maxlength) input.setAttribute('maxlength', String(def.maxlength));
        if (def.minlength) input.setAttribute('minlength', String(def.minlength));
        break;
      case 'select':
        input = el('select', common);
        (def.options || []).forEach(opt => {
          const o = typeof opt === 'string' ? { value: opt, label: opt } : opt;
          input.appendChild(el('option', { value: o.value }, o.label || o.value));
        });
        break;
      case 'checkbox':
      case 'switch':
        input = el('input', Object.assign({}, common, { type: 'checkbox' }));
        break;
      case 'number':
        input = el('input', Object.assign({}, common, { type: 'number' }));
        if (def.min !== undefined) input.setAttribute('min', String(def.min));
        if (def.max !== undefined) input.setAttribute('max', String(def.max));
        break;
      default: // text/password/etc.
        input = el('input', Object.assign({}, common, { type: def.type || 'text' }));
        if (def.maxlength) input.setAttribute('maxlength', String(def.maxlength));
        if (def.minlength) input.setAttribute('minlength', String(def.minlength));
    }

    if (def.required) input.setAttribute('required', 'true');

    const help = def.help ? el('div', { class: 'help' }, def.help) : null;
    const err = el('div', { class: 'error', id: id + '_err', 'aria-live': 'polite' });

    // Real-time validation
    const onValidate = () => {
      const val = (def.type === 'checkbox' || def.type === 'switch') ? input.checked : input.value;
      validateField(def, input, val);
      updateSaveState();
    };
    const isTextual = (def.type === 'text' || def.type === 'password' || def.type === 'textarea' || !def.type);
    const debounced = isTextual ? debounce(onValidate, 140) : onValidate;

    input.addEventListener('input', debounced);
    input.addEventListener('change', onValidate);
    input.addEventListener('blur', onValidate);

    if (def.type === 'checkbox' || def.type === 'switch') {
      const row = el('div', { class: 'inline' }, [input, label]);
      wrap.appendChild(row);
      if (help) wrap.appendChild(help);
      wrap.appendChild(err);
    } else {
      wrap.appendChild(label);
      wrap.appendChild(input);
      if (help) wrap.appendChild(help);
      wrap.appendChild(err);
    }

    // Apply default if present
    if (Object.prototype.hasOwnProperty.call(def, 'default')) {
      if (def.type === 'checkbox' || def.type === 'switch') input.checked = Boolean(def.default);
      else input.value = String(def.default);
    }

    return wrap;
  }

  function createSection(sec) {
    const card = el('fieldset', { class: 'card' });
    if (sec.legend) card.appendChild(el('legend', {}, sec.legend));
    const grid = el('div', { class: 'form-grid' });
    (sec.fields || []).forEach(fd => grid.appendChild(createField(fd)));
    card.appendChild(grid);
    return card;
  }

  function collectData(schema) {
    const out = {};
    (schema.pages || []).forEach(pg => {
      (pg.sections || []).forEach(sec => {
        (sec.fields || []).forEach(def => {
          const input = document.querySelector(`[name="${cssEscape(def.name)}"]`);
          if (!input) return;
          let val;
          if (def.type === 'checkbox' || def.type === 'switch') val = input.checked;
          else if (def.type === 'number') {
            const num = Number(input.value);
            val = Number.isFinite(num) ? num : null;
          } else val = input.value;
          setNested(out, def.name, val);
        });
      });
    });
    return out;
  }

  function applyData(schema, data) {
    (schema.pages || []).forEach(pg => {
      (pg.sections || []).forEach(sec => {
        (sec.fields || []).forEach(def => {
          const input = document.querySelector(`[name="${cssEscape(def.name)}"]`);
          if (!input) return;
          const val = getNested(data, def.name);
          if (val === undefined) return;
          if (def.type === 'checkbox' || def.type === 'switch') input.checked = Boolean(val);
          else input.value = String(val);
        });
      });
    });
    validateAll(schema);
    updateSaveState();
  }

  async function loadDefaults() {
    try {
      const res = await fetch('/config', { method: 'GET', headers: { 'Accept': 'application/json' } });
      if (!res.ok) throw new Error('GET /config failed');
      const data = await res.json();
      applyData(window._schema, data);
      showToast('Configuration loaded');
    } catch (e) {
      showToast('Could not load defaults');
      console.warn(e);
    }
  }

  function firstInvalidInput(schema) {
    for (const pg of (schema.pages || [])) {
      for (const sec of (pg.sections || [])) {
        for (const def of (sec.fields || [])) {
          const input = document.querySelector(`[name="${cssEscape(def.name)}"]`);
          const ok = validateField(def, input);
          if (!ok) return input;
        }
      }
    }
    return null;
  }

  async function saveAll() {
    const anyBad = !validateAll(window._schema);
    updateSaveState();
    if (anyBad) {
      const firstBad = firstInvalidInput(window._schema);
      if (firstBad) firstBad.scrollIntoView({ behavior: 'smooth', block: 'center' });
      return;
    }

    const payload = collectData(window._schema);

    try {
      const res = await fetch('/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      if (!res.ok) throw new Error('POST /config failed');
      showToast('Configuration saved');
    } catch (e) {
      showToast('Save failed');
      console.warn(e);
    }
  }

  async function callEndpoint(btn) {
    try {
      if (btn.confirm && !confirm(btn.confirm)) return;
      let body = undefined;
      const headers = {};
      if ((btn.method || 'GET').toUpperCase() === 'POST') {
        if (btn.includeForm) {
          body = JSON.stringify(collectData(window._schema));
          headers['Content-Type'] = 'application/json';
        } else if (btn.payload) {
          body = JSON.stringify(btn.payload);
          headers['Content-Type'] = 'application/json';
        }
      }
      const res = await fetch(btn.endpoint, { method: btn.method || 'GET', headers, body });
      if (!res.ok) throw new Error('Call failed');
      showToast((btn.label || 'Action') + ' done');
    } catch (e) {
      showToast((btn.label || 'Action') + ' failed');
      console.warn(e);
    }
  }

  // Save buttons registry
  let _saveButtons = [];
  function updateSaveState() {
    const ok = validateAll(window._schema);
    _saveButtons.forEach(b => b.disabled = !ok);
  }

  // ----- Theme toggle (light/dark/auto) -----
  function applyTheme(theme) { // 'light' | 'dark' | null
    const root = document.documentElement;
    if (theme === 'light') {
      root.setAttribute('data-theme', 'light');
      localStorage.setItem('theme', 'light');
    } else if (theme === 'dark') {
      root.setAttribute('data-theme', 'dark');
      localStorage.setItem('theme', 'dark');
    } else {
      root.removeAttribute('data-theme'); // back to auto
      localStorage.removeItem('theme');
    }
  }
  function initTheme() {
    const saved = localStorage.getItem('theme'); // 'light'|'dark'|null
    applyTheme(saved || null);
  }
  function toggleTheme() {
    const root = document.documentElement;
    const current = root.getAttribute('data-theme'); // 'light'|'dark'|null
    if (current === 'dark') applyTheme('light');
    else if (current === 'light') applyTheme(null); // go to auto
    else applyTheme('dark');
  }

  function render(schema) {
    window._schema = schema;

    // Theme (accent color from schema)
    const accent = (schema.theme && schema.theme.accent) || getComputedStyle(document.documentElement).getPropertyValue('--accent') || '';
    if (accent) document.documentElement.style.setProperty('--accent', accent);

    // Tabs
    const tabsWrap = $('#tabs');
    tabsWrap.innerHTML = '';
    (schema.pages || []).forEach((pg, i) => {
      const t = el('button', { class: 'tab' + (i === 0 ? ' active' : ''), 'data-target': pg.id }, pg.title || pg.id);
      t.addEventListener('click', () => {
        $$('.tab', tabsWrap).forEach(x => x.classList.remove('active'));
        t.classList.add('active');
        $$('.page').forEach(p => p.style.display = (p.id === 'page_' + pg.id ? 'block' : 'none'));
      });
      tabsWrap.appendChild(t);
    });

    // Pages
    const pages = $('#pages');
    pages.innerHTML = '';
    (schema.pages || []).forEach((pg, i) => {
      const pageEl = el('div', { class: 'page', id: 'page_' + pg.id, style: 'display:' + (i === 0 ? 'block' : 'none') });
      (pg.sections || []).forEach(sec => pageEl.appendChild(createSection(sec)));

      // Page-level buttons
      if (Array.isArray(pg.buttons) && pg.buttons.length) {
        const card = el('div', { class: 'card' });
        const row = el('div', { class: 'btn-row' });
        pg.buttons.forEach(btn => {
          const b = el('button', { class: 'btn outline' }, btn.label || 'Action');
          b.addEventListener('click', () => callEndpoint(btn));
          row.appendChild(b);
        });
        card.appendChild(row);
        pageEl.appendChild(card);
      }

      pages.appendChild(pageEl);
    });

    // Default buttons
    const defWrap = $('#defaultButtons');
    defWrap.innerHTML = '';
    _saveButtons = [];
    (schema.defaultButtons || [{ label: 'Save All', kind: 'save' }]).forEach(btn => {
      let b;
      if (btn.kind === 'save') {
        b = el('button', { class: 'btn' }, btn.label || 'Save');
        b.addEventListener('click', saveAll);
        _saveButtons.push(b);
      } else if (btn.kind === 'load') {
        b = el('button', { class: 'btn outline' }, btn.label || 'Reload');
        b.addEventListener('click', loadDefaults);
      } else {
        b = el('button', { class: 'btn outline' }, btn.label || 'Action');
        b.addEventListener('click', () => callEndpoint(btn));
      }
      defWrap.appendChild(b);
    });

    // Theme toggle button
    initTheme();
    const themeBtn = document.getElementById('themeToggle');
    if (themeBtn) themeBtn.addEventListener('click', toggleTheme);

    // Initial validation state (disabled until valid)
    updateSaveState();

    // Initial load from /config
    loadDefaults();
  }

  // Bootstrap: use inline schema if available; else fetch external
  function bootstrap() {
    const tag = document.getElementById('schema');
    if (tag) {
      try { const schema = JSON.parse(tag.textContent); render(schema); return; }
      catch (e) { console.warn('Bad inline schema:', e); }
    }
    const url = window.__SCHEMA_URL__ || 'schema.json';
    fetch(url).then(r => { if (!r.ok) throw new Error('Cannot load schema'); return r.json(); })
      .then(render)
      .catch(err => { console.error(err); showToast('Schema load failed'); });
  }
  document.addEventListener('DOMContentLoaded', bootstrap);
})();