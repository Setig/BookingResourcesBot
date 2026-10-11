(function () {
  const localeKey = 'bookingResources.locale';
  const dictionaryCacheKey = 'bookingResources.i18nDictionaries';
  const pendingLoads = new Map();
  let dictionary = {};
  let loadedLocale = '';
  let resolvedLocale = '';
  let applyToken = 0;

  function normalizedLocale(value) {
    const raw = String(value || '').toLowerCase().replace(/-/g, '_');
    if (raw === 'ru_stand') {
      return 'ru_stand';
    }
    if (raw.startsWith('ru')) {
      return 'ru';
    }
    if (raw.startsWith('en')) {
      return 'en';
    }
    return '';
  }

  function browserLocale() {
    const candidates =
      Array.isArray(navigator.languages) && navigator.languages.length
        ? navigator.languages
        : [navigator.language];
    for (let i = 0; i < candidates.length; i += 1) {
      const normalized = normalizedLocale(candidates[i]);
      if (normalized) {
        return normalized;
      }
    }
    return 'en';
  }

  function storedLocale() {
    const raw = localStorage.getItem(localeKey);
    if (raw == null || raw === '' || String(raw).toLowerCase() === 'default') {
      return '';
    }
    return normalizedLocale(raw);
  }

  function currentLocale() {
    return resolvedLocale || storedLocale() || browserLocale();
  }

  function primaryLocale(locale) {
    const resolved = normalizedLocale(locale || currentLocale());
    return resolved === 'ru_stand' ? 'ru' : resolved;
  }

  function intlLocale(locale) {
    return primaryLocale(locale);
  }

  function documentLang(locale) {
    return locale === 'ru_stand' ? 'ru' : locale;
  }

  function hasDictionaryEntries(value) {
    return Boolean(value) && typeof value === 'object' && Object.keys(value).length > 0;
  }

  function setDocumentLocale(locale) {
    resolvedLocale = locale;
    document.documentElement.lang = documentLang(locale);
  }

  function revealTranslations() {
    document.documentElement.removeAttribute('data-i18n-pending');
  }

  function readCachedDictionaries() {
    try {
      const parsed = JSON.parse(localStorage.getItem(dictionaryCacheKey) || '');
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
        return {};
      }
      return parsed;
    } catch (error) {
      return {};
    }
  }

  function readCachedDictionary(locale) {
    const cached = readCachedDictionaries()[locale];
    if (!hasDictionaryEntries(cached)) {
      return null;
    }
    return cached;
  }

  function writeCachedDictionary(locale, value) {
    if (!hasDictionaryEntries(value)) {
      return;
    }
    try {
      const dictionaries = readCachedDictionaries();
      dictionaries[locale] = value;
      localStorage.setItem(dictionaryCacheKey, JSON.stringify(dictionaries));
    } catch (error) {
      // Ignore quota or private-mode failures.
    }
  }

  async function fetchDictionary(locale) {
    try {
      const response = await fetch(`/api/i18n?locale=${encodeURIComponent(locale)}`, {
        credentials: 'same-origin',
      });
      if (!response.ok) {
        return {};
      }
      const payload = await response.json();
      return hasDictionaryEntries(payload) ? payload : {};
    } catch (error) {
      return {};
    }
  }

  function loadDictionary(locale) {
    if (pendingLoads.has(locale)) {
      return pendingLoads.get(locale);
    }
    const request = fetchDictionary(locale).finally(() => {
      pendingLoads.delete(locale);
    });
    pendingLoads.set(locale, request);
    return request;
  }

  function text(key, fallback = '') {
    return dictionary[key] || fallback || key;
  }

  function fallbackDataKey(kind) {
    return `i18nFallback${kind.charAt(0).toUpperCase()}${kind.slice(1)}`;
  }

  function fallbackValue(element, kind, currentValue) {
    const key = fallbackDataKey(kind);
    if (!Object.prototype.hasOwnProperty.call(element.dataset, key)) {
      element.dataset[key] = currentValue || '';
    }
    return element.dataset[key];
  }

  function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach((element) => {
      const fallback = fallbackValue(element, 'text', element.textContent);
      element.textContent = text(element.dataset.i18n, fallback);
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach((element) => {
      const fallback = fallbackValue(element, 'placeholder', element.getAttribute('placeholder'));
      element.setAttribute('placeholder', text(element.dataset.i18nPlaceholder, fallback));
    });
    document.querySelectorAll('[data-i18n-title]').forEach((element) => {
      const fallback = fallbackValue(element, 'title', element.getAttribute('title'));
      element.setAttribute('title', text(element.dataset.i18nTitle, fallback));
    });
    document.querySelectorAll('[data-i18n-aria-label]').forEach((element) => {
      const fallback = fallbackValue(element, 'ariaLabel', element.getAttribute('aria-label'));
      element.setAttribute('aria-label', text(element.dataset.i18nAriaLabel, fallback));
    });
  }

  function applyDictionary(locale, value) {
    dictionary = value;
    loadedLocale = locale;
    setDocumentLocale(locale);
    applyTranslations();
  }

  function resolvePreference(locale) {
    let preference;
    if (locale == null) {
      preference = storedLocale();
    } else if (locale === '' || String(locale).toLowerCase() === 'default') {
      preference = '';
    } else {
      preference = normalizedLocale(locale);
    }
    return {
      preference,
      resolved: preference || browserLocale(),
    };
  }

  async function apply(locale, options = {}) {
    const persist = options.persist !== false;
    const { preference, resolved } = resolvePreference(locale);
    if (persist) {
      localStorage.setItem(localeKey, preference);
    }

    const token = ++applyToken;
    const isCurrent = () => token === applyToken;

    if (loadedLocale === resolved && hasDictionaryEntries(dictionary)) {
      setDocumentLocale(resolved);
      applyTranslations();
      revealTranslations();
      return dictionary;
    }

    const cached = readCachedDictionary(resolved);
    if (cached) {
      applyDictionary(resolved, cached);
      revealTranslations();
    } else {
      setDocumentLocale(resolved);
    }

    const nextDictionary = await loadDictionary(resolved);
    if (!isCurrent()) {
      return dictionary;
    }

    if (hasDictionaryEntries(nextDictionary)) {
      applyDictionary(resolved, nextDictionary);
      writeCachedDictionary(resolved, nextDictionary);
    } else if (!cached) {
      applyTranslations();
    }
    revealTranslations();
    return dictionary;
  }

  const ready = apply(browserLocale(), { persist: false });

  function whenReady() {
    return ready;
  }

  window.BookingResourcesI18n = {
    key: localeKey,
    currentLocale,
    browserLocale,
    primaryLocale,
    intlLocale,
    apply,
    whenReady,
    ready,
    text,
  };
})();
