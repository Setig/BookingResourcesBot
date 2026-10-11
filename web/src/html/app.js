(function () {
  const profileKey = 'bookingResources.profile';
  const historyKey = 'bookingResources.chatHistory';
  const historyLimitKey = 'bookingResources.historyLimit';
  const commandCacheKey = 'bookingResources.commandCache';
  const defaultPageTitle = 'Booking Resources';
  const defaultHistoryLimit = 200;
  const minHistoryLimit = 20;
  const maxHistoryLimit = 1000;
  let activeUserId = '';
  let runtimeConfigPromise = null;

  function userSuffix(userId) {
    const value = String(userId || activeUserId || '').trim();
    return value ? `.${value}` : '';
  }

  function userScopedKey(baseKey, userId) {
    return `${baseKey}${userSuffix(userId)}`;
  }

  function translatedText(key, fallback) {
    if (window.BookingResourcesI18n && typeof window.BookingResourcesI18n.text === 'function') {
      const text = window.BookingResourcesI18n.text(key);
      if (text && text !== key) {
        return text;
      }
    }
    return fallback;
  }

  function normalizedPageTitle(value) {
    const title = String(value || '').trim();
    return title || defaultPageTitle;
  }

  async function request(path, options) {
    let response;
    const locale =
      window.BookingResourcesI18n && typeof window.BookingResourcesI18n.currentLocale === 'function'
        ? window.BookingResourcesI18n.currentLocale()
        : '';
    try {
      response = await fetch(path, {
        credentials: 'same-origin',
        headers: {
          'Content-Type': 'application/json',
          ...(locale ? { 'X-Booking-Resources-Locale': locale } : {}),
          ...(options && options.headers ? options.headers : {}),
        },
        ...options,
      });
    } catch (cause) {
      const error = new Error(
        translatedText('web_server_unavailable', 'Web server is not available.')
      );
      error.network = true;
      error.cause = cause;
      throw error;
    }

    const contentType = response.headers.get('content-type') || '';
    const body = contentType.includes('application/json') ? await response.json() : null;
    if (!response.ok) {
      const errorCode = body && body.error_code;
      const serverUnavailable =
        errorCode === 'server_unavailable' || response.status === 503;
      const error = new Error(
        serverUnavailable
          ? translatedText('server_unavailable', 'Server is not available.')
          : (body && body.error) || response.statusText
      );
      error.status = response.status;
      error.body = body;
      if (serverUnavailable) {
        error.serverUnavailable = true;
      }
      throw error;
    }
    return body;
  }

  async function session() {
    try {
      return await request('/api/session', { method: 'GET' });
    } catch (error) {
      return { ok: false, status: error.status };
    }
  }

  async function runtimeConfig() {
    if (!runtimeConfigPromise) {
      runtimeConfigPromise = request('/api/runtime-config', { method: 'GET' }).catch(() => ({}));
    }
    return runtimeConfigPromise;
  }

  function normalizeAuthMethods(methods) {
    const aliases = {
      'web-profile': 'sign-without-registration',
      'local-password': 'log-in-with-registration',
      'sign-without-registration': 'sign-without-registration',
      'log-in-only': 'log-in-only',
      'log-in-with-registration': 'log-in-with-registration',
    };
    const result = [];
    const seen = new Set();
    (Array.isArray(methods) ? methods : []).forEach((value) => {
      String(value || '')
        .split(',')
        .forEach((part) => {
          const method = aliases[part.trim().toLowerCase().replace(/_/g, '-')];
          if (method && !seen.has(method)) {
            seen.add(method);
            result.push(method);
          }
        });
    });
    if (seen.has('log-in-only') && seen.has('log-in-with-registration')) {
      return result.filter((method) => method !== 'log-in-only');
    }
    return result;
  }

  function authCapabilities(config) {
    const configured = Array.isArray(config && config.auth_methods);
    const methods = normalizeAuthMethods(config && config.auth_methods);
    const resolved = methods.length
      ? methods
      : configured
        ? []
        : ['sign-without-registration'];
    return {
      methods: resolved,
      allowsSignWithoutRegistration: resolved.includes('sign-without-registration'),
      allowsLogin: resolved.includes('log-in-only') || resolved.includes('log-in-with-registration'),
      allowsRegistration: resolved.includes('log-in-with-registration'),
    };
  }

  async function pageTitleBase() {
    const config = await runtimeConfig();
    return normalizedPageTitle(config && config.page_title);
  }

  async function applyPageTitle(sectionKey = '', fallback = '') {
    const appTitle = await pageTitleBase();
    document.querySelectorAll('[data-app-name]').forEach((element) => {
      element.textContent = appTitle;
    });

    const sectionTitle = sectionKey ? String(translatedText(sectionKey, fallback)).trim() : '';
    document.title = sectionTitle ? `${sectionTitle} ${appTitle}` : appTitle;
    return document.title;
  }

  function saveProfile(profile) {
    const normalized = profile || {};
    if (normalized.user_id) {
      setActiveUserId(normalized.user_id);
    }
    localStorage.setItem(userScopedKey(profileKey, normalized.user_id), JSON.stringify(normalized));
  }

  function loadProfile() {
    try {
      return JSON.parse(localStorage.getItem(userScopedKey(profileKey)) || '{}');
    } catch (error) {
      return {};
    }
  }

  function clearProfile() {
    localStorage.removeItem(userScopedKey(profileKey));
  }

  function setActiveUserId(userId) {
    activeUserId = String(userId || '').trim();
  }

  function formatWebUserId(userId) {
    const trimmed = String(userId || '').trim();
    return trimmed ? `web:${trimmed}` : 'web:';
  }

  function accessActivationPendingNoticeParts(userId) {
    return {
      notice: translatedText(
        'access_activation_pending_notice',
        'Contact an administrator to request access activation.'
      ),
      label: translatedText('user_id', 'Your User ID:'),
      userId: formatWebUserId(userId),
    };
  }

  function accessActivationPendingNoticeText(userId) {
    const parts = accessActivationPendingNoticeParts(userId);
    return `${parts.notice} ${parts.label} ${parts.userId}`;
  }

  function accessActivationPendingNoticeMessage(userId) {
    return {
      id: `access-activation-pending-${userId || ''}`,
      type: 'meta',
      kind: 'access_activation_pending',
      user_id: userId,
      text: accessActivationPendingNoticeText(userId),
    };
  }

  function historyLimit() {
    const value = Number(localStorage.getItem(historyLimitKey) || defaultHistoryLimit);
    if (!Number.isFinite(value)) {
      return defaultHistoryLimit;
    }
    return Math.min(maxHistoryLimit, Math.max(minHistoryLimit, Math.round(value)));
  }

  function setHistoryLimit(value) {
    const normalized = Math.min(maxHistoryLimit, Math.max(minHistoryLimit, Math.round(Number(value))));
    localStorage.setItem(historyLimitKey, String(normalized));
    trimHistory();
    return normalized;
  }

  function loadHistory() {
    try {
      const value = JSON.parse(localStorage.getItem(userScopedKey(historyKey)) || '[]');
      return Array.isArray(value) ? value : [];
    } catch (error) {
      return [];
    }
  }

  function saveHistory(messages) {
    const limit = historyLimit();
    const trimmed = messages.slice(Math.max(0, messages.length - limit));
    localStorage.setItem(userScopedKey(historyKey), JSON.stringify(trimmed));
    return trimmed;
  }

  function appendHistory(message) {
    const messages = loadHistory();
    messages.push({
      id: message.id || `${Date.now()}-${Math.random().toString(16).slice(2)}`,
      timestamp: message.timestamp || new Date().toISOString(),
      ...message,
    });
    return saveHistory(messages);
  }

  function trimHistory() {
    return saveHistory(loadHistory());
  }

  function clearHistory() {
    localStorage.removeItem(userScopedKey(historyKey));
  }

  function clearCommandCache(locale) {
    const key = userScopedKey(commandCacheKey);
    const normalizedLocale = String(locale || '').trim();
    if (!normalizedLocale) {
      localStorage.removeItem(key);
      return;
    }

    try {
      const cache = JSON.parse(localStorage.getItem(key) || '{}');
      if (cache && typeof cache === 'object') {
        delete cache[normalizedLocale];
        localStorage.setItem(key, JSON.stringify(cache));
      }
    } catch (error) {
      localStorage.removeItem(key);
    }
  }

  function redirectToAuthorize() {
    window.location.href = '/authorize.html';
  }

  async function logout() {
    try {
      await request('/api/logout', { method: 'POST' });
    } finally {
      clearProfile();
      setActiveUserId('');
      redirectToAuthorize();
    }
  }

  window.BookingResourcesApp = {
    keys: {
      profile: profileKey,
      history: historyKey,
      historyLimit: historyLimitKey,
      commandCache: commandCacheKey,
    },
    setActiveUserId,
    formatWebUserId,
    accessActivationPendingNoticeParts,
    accessActivationPendingNoticeText,
    accessActivationPendingNoticeMessage,
    request,
    session,
    runtimeConfig,
    authCapabilities,
    pageTitleBase,
    applyPageTitle,
    saveProfile,
    loadProfile,
    clearProfile,
    userScopedKey,
    historyLimit,
    setHistoryLimit,
    loadHistory,
    saveHistory,
    appendHistory,
    trimHistory,
    clearHistory,
    clearCommandCache,
    redirectToAuthorize,
    logout,
  };
})();
