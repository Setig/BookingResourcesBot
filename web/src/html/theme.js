(function () {
  const storageKey = 'bookingResources.theme';
  const themeTransitionDurationMs = 180;
  const themeTransitionMaxDelayMs = 140;
  const themeTransitionStepMs = 18;
  let themeTransitionTimer = 0;

  function preferredTheme() {
    const stored = localStorage.getItem(storageKey);
    if (stored === 'light' || stored === 'dark') {
      return stored;
    }
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches
      ? 'dark'
      : 'light';
  }

  function prefersReducedMotion() {
    return window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  }

  const visualBlockLevels = [
    ['.top-actions', 1],
    ['.card, .chat-content', 1],
    ['.settings-group, .message-list, .message-form, .chat-composer, .command-suggestions', 2],
    ['label, .actions, .message, .message-day-separator', 3],
    ['input, select, textarea, button, a.button, .message-text, .message-meta-info, .message-keyboard', 4],
  ];

  function visualBlockLevel(element) {
    let matchedLevel = 0;
    for (const [selector, level] of visualBlockLevels) {
      if (element.matches(selector) || element.closest(selector)) {
        matchedLevel = level;
      }
    }
    return matchedLevel;
  }

  function clearThemeTransitionDelays() {
    document.documentElement.classList.remove('theme-transitioning');
    if (!document.body) {
      return;
    }

    [document.body, ...document.body.querySelectorAll('*')].forEach((element) => {
      delete element.dataset.themeTransitionLevel;
    });
  }

  function startThemeTransition() {
    if (!document.body || prefersReducedMotion()) {
      return;
    }

    window.clearTimeout(themeTransitionTimer);
    let maxDelay = 0;
    [document.body, ...document.body.querySelectorAll('*')].forEach((element) => {
      const delay = Math.min(visualBlockLevel(element) * themeTransitionStepMs, themeTransitionMaxDelayMs);
      maxDelay = Math.max(maxDelay, delay);
      element.dataset.themeTransitionLevel = String(visualBlockLevel(element));
    });
    document.documentElement.classList.add('theme-transitioning');
    themeTransitionTimer = window.setTimeout(
      clearThemeTransitionDelays,
      maxDelay + themeTransitionDurationMs + 80
    );
  }

  function createSvgElement(tagName, attributes) {
    const element = document.createElementNS('http://www.w3.org/2000/svg', tagName);
    Object.entries(attributes || {}).forEach(([name, value]) => {
      element.setAttribute(name, value);
    });
    return element;
  }

  function createThemeIcon(theme) {
    const svg = createSvgElement('svg', {
      viewBox: '0 0 24 24',
      'aria-hidden': 'true',
      focusable: 'false',
    });

    if (theme === 'dark') {
      svg.appendChild(createSvgElement('circle', {
        cx: '12',
        cy: '12',
        r: '4',
        fill: 'currentColor',
      }));
      [
        ['12', '2', '12', '4'],
        ['12', '20', '12', '22'],
        ['4.93', '4.93', '6.34', '6.34'],
        ['17.66', '17.66', '19.07', '19.07'],
        ['2', '12', '4', '12'],
        ['20', '12', '22', '12'],
        ['4.93', '19.07', '6.34', '17.66'],
        ['17.66', '6.34', '19.07', '4.93'],
      ].forEach(([x1, y1, x2, y2]) => {
        svg.appendChild(createSvgElement('line', { x1, y1, x2, y2 }));
      });
      return svg;
    }

    svg.appendChild(createSvgElement('circle', { cx: '12', cy: '12', r: '8' }));
    svg.appendChild(createSvgElement('path', {
      d: 'M12 4A8 8 0 0 0 12 20Z',
      fill: 'currentColor',
      stroke: 'none',
    }));
    return svg;
  }

  function applyTheme(theme, animate = false) {
    const resolved = theme === 'dark' ? 'dark' : 'light';
    const current = document.documentElement.dataset.theme || '';
    if (animate && current && current !== resolved) {
      startThemeTransition();
    }
    document.documentElement.dataset.theme = resolved;
    localStorage.setItem(storageKey, resolved);
    document.querySelectorAll('[data-theme-toggle]').forEach((button) => {
      button.replaceChildren(createThemeIcon(resolved));
    });
  }

  window.BookingResourcesTheme = {
    key: storageKey,
    current: preferredTheme,
    apply: applyTheme,
    toggle() {
      applyTheme(preferredTheme() === 'dark' ? 'light' : 'dark', true);
    },
  };

  applyTheme(preferredTheme());
  window.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('[data-theme-toggle]').forEach((button) => {
      button.addEventListener('click', () => window.BookingResourcesTheme.toggle());
    });
  });
})();
