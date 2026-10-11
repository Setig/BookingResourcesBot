(function () {
  function elementById(root, id) {
    if (root && root !== document && typeof root.querySelector === 'function') {
      return root.querySelector(`#${id}`);
    }
    return document.getElementById(id);
  }

  function copyTextFallback(text) {
    const textArea = document.createElement('textarea');
    textArea.value = text;
    textArea.setAttribute('readonly', '');
    textArea.className = 'clipboard-copy-proxy';
    document.body.appendChild(textArea);
    textArea.select();
    const copied = document.execCommand('copy');
    textArea.remove();
    if (!copied) {
      throw new Error('Copy command failed.');
    }
  }

  async function copyText(text) {
    if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
      await navigator.clipboard.writeText(text);
      return;
    }
    copyTextFallback(text);
  }

  const timezoneOffsets = [
    -720,
    -660,
    -630,
    -600,
    -570,
    -540,
    -525,
    -480,
    -420,
    -390,
    -360,
    -345,
    -330,
    -300,
    -270,
    -240,
    -210,
    -180,
    -120,
    -60,
    0,
    60,
    120,
    180,
    210,
    240,
    270,
    300,
    330,
    345,
    360,
    390,
    420,
    480,
    525,
    540,
    570,
    600,
    630,
    660,
    720,
    780,
    840,
  ];

  function formatTimezoneOffset(offsetMinutes) {
    const minutes = Number(offsetMinutes);
    if (!Number.isFinite(minutes) || minutes === 0) {
      return 'UTC';
    }
    const sign = minutes > 0 ? '+' : '-';
    const absoluteMinutes = Math.abs(minutes);
    const hours = String(Math.floor(absoluteMinutes / 60)).padStart(2, '0');
    const remainder = String(absoluteMinutes % 60).padStart(2, '0');
    return `UTC${sign}${hours}:${remainder}`;
  }

  function normalizeSettings(settings) {
    return {
      locale: String((settings && settings.locale) || ''),
      timezone: String((settings && settings.timezone) || ''),
      occupation_notify: String((settings && settings.occupation_notify) || 'mine'),
    };
  }

  function supportedLocaleValue(locale) {
    const normalized = String(locale || '').toLowerCase().replace(/-/g, '_');
    if (normalized === 'ru_stand') {
      return 'ru_stand';
    }
    if (normalized.startsWith('ru')) {
      return 'ru';
    }
    if (normalized.startsWith('en')) {
      return 'en';
    }
    return '';
  }

  function bindNumberFieldSteps(input) {
    const field = input && input.closest('.number-field');
    if (!field) {
      return;
    }

    field.querySelectorAll('[data-number-step]').forEach((button) => {
      button.addEventListener('mousedown', (event) => {
        event.preventDefault();
      });
      button.addEventListener('click', () => {
        if (input.disabled) {
          return;
        }
        try {
          if (button.getAttribute('data-number-step') === 'up') {
            input.stepUp();
          } else {
            input.stepDown();
          }
        } catch (error) {
          // Native step methods throw when the current value is invalid.
        }
      });
    });
  }

  function preferredFormLocale(response, sessionLocale) {
    if (response && response.access_active === false) {
      return supportedLocaleValue(sessionLocale);
    }
    const settings = (response && response.settings) || {};
    return supportedLocaleValue(settings.locale);
  }

  async function saveWebProfileLocale(locale) {
    const response = await window.BookingResourcesApp.request('/api/profile', {
      method: 'POST',
      body: JSON.stringify({ locale }),
    });
    window.BookingResourcesApp.saveProfile(response);
    return response;
  }

  async function init(options = {}) {
    const root = options.root || document;
    const form = elementById(root, 'settingsForm');
    if (!form || form.dataset.settingsInitialized === 'true') {
      return null;
    }

    form.dataset.settingsInitialized = 'true';

    const session = options.session || await window.BookingResourcesApp.session();
    if (!session.ok) {
      window.BookingResourcesApp.redirectToAuthorize();
      return null;
    }
    window.BookingResourcesApp.setActiveUserId(session.user_id);

    const themeSelect = elementById(root, 'themeSelect');
    const localeSelect = elementById(root, 'localeSelect');
    const timezoneInput = elementById(root, 'timezoneInput');
    const occupationNotifySelect = elementById(root, 'occupationNotifySelect');
    const userIdInput = elementById(root, 'userIdInput');
    const copyUserIdButton = elementById(root, 'copyUserIdButton');
    const historyLimitInput = elementById(root, 'historyLimitInput');
    const statusBox = elementById(root, 'settingsStatus');
    const localPasswordSettings = elementById(root, 'localPasswordSettings');
    const localPasswordLoginValue = elementById(root, 'localPasswordLoginValue');
    const changePasswordDialog = elementById(root, 'changePasswordDialog');
    const changePasswordForm = elementById(root, 'changePasswordForm');
    const changePasswordCloseButton = elementById(root, 'changePasswordCloseButton');
    const changePasswordCancelButton = elementById(root, 'changePasswordCancelButton');
    const changePasswordStatus = elementById(root, 'changePasswordStatus');
    const currentPasswordInput = elementById(root, 'currentPasswordInput');
    const newPasswordInput = elementById(root, 'newPasswordInput');
    const confirmNewPasswordInput = elementById(root, 'confirmNewPasswordInput');
    const changePasswordButton = elementById(root, 'changePasswordButton');
    const clearHistoryButton = elementById(root, 'clearHistoryButton');
    const logoutButton = elementById(root, 'logoutButton');
    const settingsOkButton = elementById(root, 'settingsOkButton');
    const serverSettingFields = [timezoneInput, occupationNotifySelect].filter(Boolean);
    let userSettingsAvailable = true;
    let copyUserIdFeedbackTimer = 0;
    let clearHistoryFeedbackTimer = 0;

    function showCopyUserIdFeedback(className) {
      window.clearTimeout(copyUserIdFeedbackTimer);
      copyUserIdButton.classList.remove('copied', 'failed');
      copyUserIdButton.classList.add(className);
      copyUserIdFeedbackTimer = window.setTimeout(() => {
        copyUserIdButton.classList.remove(className);
      }, 1500);
    }

    function showClearHistoryFeedback() {
      window.clearTimeout(clearHistoryFeedbackTimer);
      clearHistoryButton.classList.add('confirmed');
      clearHistoryFeedbackTimer = window.setTimeout(() => {
        clearHistoryButton.classList.remove('confirmed');
      }, 1500);
    }

    function showStatus(key, warning = false) {
      statusBox.textContent = window.BookingResourcesI18n.text(key);
      statusBox.classList.toggle('warning', warning);
      statusBox.hidden = false;
    }

    function showMessage(message) {
      statusBox.textContent = message;
      statusBox.classList.remove('warning');
      statusBox.hidden = !message;
    }

    function showPasswordDialogMessage(message) {
      changePasswordStatus.textContent = message;
      changePasswordStatus.hidden = !message;
    }

    function resetPasswordDialog() {
      changePasswordForm.reset();
      showPasswordDialogMessage('');
    }

    function openPasswordDialog() {
      resetPasswordDialog();
      changePasswordDialog.hidden = false;
      currentPasswordInput.focus();
    }

    function closePasswordDialog() {
      changePasswordDialog.hidden = true;
      resetPasswordDialog();
      changePasswordButton.focus();
    }

    function populateTimezoneOptions() {
      timezoneInput.innerHTML = '';

      const defaultOption = document.createElement('option');
      defaultOption.value = '';
      defaultOption.textContent = window.BookingResourcesI18n.text('default_server_timezone');
      timezoneInput.appendChild(defaultOption);

      timezoneOffsets.forEach((offsetMinutes) => {
        const option = document.createElement('option');
        option.value = String(offsetMinutes);
        option.textContent = formatTimezoneOffset(offsetMinutes);
        timezoneInput.appendChild(option);
      });
    }

    function updateTimezoneDefaultOption() {
      const defaultOption = timezoneInput.querySelector('option[value=""]');
      if (defaultOption) {
        defaultOption.textContent = window.BookingResourcesI18n.text('default_server_timezone');
      }
    }

    function ensureTimezoneOption(value) {
      const normalized = String(value || '').trim();
      if (!normalized || Array.from(timezoneInput.options).some((option) => option.value === normalized)) {
        return;
      }

      const option = document.createElement('option');
      option.value = normalized;
      option.textContent = formatTimezoneOffset(normalized);
      timezoneInput.appendChild(option);
    }

    function updateOccupationNotifyTooltips() {
      if (!occupationNotifySelect) {
        return;
      }

      const baseTooltip = window.BookingResourcesI18n.text(
        'occupation_notify_tooltip',
        occupationNotifySelect.getAttribute('title') || ''
      ).trim();
      const selectedOption =
        occupationNotifySelect.options[occupationNotifySelect.selectedIndex] || null;
      const optionTooltip = selectedOption
        ? String(selectedOption.getAttribute('title') || '').trim()
        : '';
      const tooltipText = [baseTooltip, optionTooltip].filter(Boolean).join('\n\n');
      occupationNotifySelect.setAttribute('title', tooltipText || baseTooltip);
    }

    function applySettingsToForm(settings) {
      const normalized = normalizeSettings(settings);
      if (Object.prototype.hasOwnProperty.call(settings || {}, 'locale')) {
        localeSelect.value = supportedLocaleValue(normalized.locale);
      }
      ensureTimezoneOption(normalized.timezone);
      timezoneInput.value = normalized.timezone;
      occupationNotifySelect.value = normalized.occupation_notify || 'mine';
      updateOccupationNotifyTooltips();
    }

    function setUserSettingsAvailable(available) {
      userSettingsAvailable = available;
      serverSettingFields.forEach((field) => {
        field.disabled = !available;
        const label = field.closest('label');
        if (label) {
          label.classList.toggle('setting-unavailable', !available);
        }
      });
    }

    function setSettingsSubmitBlocked(blocked) {
      if (settingsOkButton) {
        settingsOkButton.disabled = blocked;
      }
    }

    async function notifyLocaleChanged(locale, reason) {
      if (typeof options.onLocaleChanged === 'function') {
        await options.onLocaleChanged(locale, reason);
      }
    }

    async function applyLocale(locale, reason) {
      await window.BookingResourcesI18n.apply(locale);
      updateTimezoneDefaultOption();
      updateOccupationNotifyTooltips();
      const effectiveLocale = window.BookingResourcesI18n.currentLocale();
      await notifyLocaleChanged(effectiveLocale, reason);
      return effectiveLocale;
    }

    async function applyPreferredLocale(response, sessionLocale, reason) {
      return applyLocale(preferredFormLocale(response, sessionLocale), reason);
    }

    async function loadUserSettings() {
      try {
        const response = await window.BookingResourcesApp.request('/api/user-settings', {
          method: 'GET',
        });
        const settings = response.settings || {};
        const formLocale = preferredFormLocale(response, session.locale);
        applySettingsToForm({
          ...settings,
          locale: formLocale,
        });
        await applyPreferredLocale(response, session.locale, 'load');
        window.BookingResourcesApp.saveProfile({ ...session, locale: formLocale });
        if (response.access_active === false) {
          setUserSettingsAvailable(false);
          setSettingsSubmitBlocked(false);
          showStatus('user_settings_access_activation_required', true);
        } else {
          setUserSettingsAvailable(true);
          setSettingsSubmitBlocked(false);
        }
      } catch (error) {
        applySettingsToForm({ locale: session.locale || '' });
        setUserSettingsAvailable(false);
        setSettingsSubmitBlocked(true);
        if (error && (error.serverUnavailable || error.network || error.status === 503)) {
          showMessage(
            error.message ||
              window.BookingResourcesI18n.text(
                'server_unavailable',
                'Server is not available.'
              )
          );
        } else {
          showStatus('user_settings_load_failed', true);
        }
      }
    }

    themeSelect.value = window.BookingResourcesTheme.current();
    userIdInput.textContent = window.BookingResourcesApp.formatWebUserId(session.user_id);
    historyLimitInput.value = String(window.BookingResourcesApp.historyLimit());
    localPasswordLoginValue.textContent = session.local_password_login || '';
    const runtimeConfig = await window.BookingResourcesApp.runtimeConfig();
    const authCapabilities = window.BookingResourcesApp.authCapabilities(runtimeConfig);
    const showLocalPasswordSettings =
      authCapabilities.allowsLogin && !!session.local_password_login;
    localPasswordSettings.hidden = !showLocalPasswordSettings;
    changePasswordButton.hidden = !showLocalPasswordSettings;
    populateTimezoneOptions();
    applySettingsToForm({ locale: session.locale || '' });
    await loadUserSettings();

    bindNumberFieldSteps(historyLimitInput);

    themeSelect.addEventListener('change', () => {
      window.BookingResourcesTheme.apply(themeSelect.value, true);
    });

    copyUserIdButton.addEventListener('click', async () => {
      const userId = userIdInput.textContent.trim();
      if (!userId) {
        return;
      }
      try {
        await copyText(userId);
        showCopyUserIdFeedback('copied');
      } catch (error) {
        showCopyUserIdFeedback('failed');
      }
    });

    localeSelect.addEventListener('change', async () => {
      window.BookingResourcesApp.clearCommandCache();
      await applyLocale(localeSelect.value, 'preview');
    });

    occupationNotifySelect.addEventListener('change', () => {
      updateOccupationNotifyTooltips();
    });

    form.addEventListener('submit', async (event) => {
      event.preventDefault();
      if (settingsOkButton && settingsOkButton.disabled) {
        return;
      }
      window.BookingResourcesTheme.apply(themeSelect.value, true);
      window.BookingResourcesApp.setHistoryLimit(historyLimitInput.value);
      const selectedLocale = localeSelect.value;
      window.BookingResourcesApp.clearCommandCache();
      await applyLocale(selectedLocale, 'save-local');
      try {
        await saveWebProfileLocale(selectedLocale);
      } catch (error) {
        showMessage(error.message || window.BookingResourcesI18n.text('user_settings_save_failed'));
        return;
      }

      if (!userSettingsAvailable) {
        showStatus('user_settings_access_activation_required', true);
        return;
      }

      const settings = {
        locale: localeSelect.value,
        timezone: timezoneInput.value.trim(),
        occupation_notify: occupationNotifySelect.value,
      };

      try {
        const response = await window.BookingResourcesApp.request('/api/user-settings', {
          method: 'POST',
          body: JSON.stringify({ settings }),
        });
        applySettingsToForm(response.settings || settings);
        await applyPreferredLocale(response, settings.locale, 'save');
        window.BookingResourcesApp.saveProfile({ ...session, locale: settings.locale });
        if (typeof options.onSaved === 'function') {
          await options.onSaved();
        }
      } catch (error) {
        showMessage(error.message || window.BookingResourcesI18n.text('user_settings_save_failed'));
      }
    });

    clearHistoryButton.addEventListener('click', () => {
      window.BookingResourcesApp.clearHistory();
      if (typeof options.onHistoryCleared === 'function') {
        options.onHistoryCleared();
      }
      showClearHistoryFeedback();
    });

    changePasswordButton.addEventListener('click', () => {
      openPasswordDialog();
    });

    changePasswordCloseButton.addEventListener('click', () => {
      closePasswordDialog();
    });

    changePasswordCancelButton.addEventListener('click', () => {
      closePasswordDialog();
    });

    changePasswordForm.addEventListener('submit', async (event) => {
      event.preventDefault();
      if (newPasswordInput.value !== confirmNewPasswordInput.value) {
        showPasswordDialogMessage(window.BookingResourcesI18n.text('passwords_do_not_match'));
        return;
      }

      const payload = {
        current_password: currentPasswordInput.value,
        new_password: newPasswordInput.value,
      };
      try {
        await window.BookingResourcesApp.request('/api/account/password', {
          method: 'POST',
          body: JSON.stringify(payload),
        });
        closePasswordDialog();
        showStatus('password_changed');
      } catch (error) {
        showPasswordDialogMessage(
          error.message || window.BookingResourcesI18n.text('password_change_failed')
        );
      }
    });

    if (logoutButton) {
      logoutButton.addEventListener('click', () => {
        const confirmationMessage =
          logoutButton.getAttribute('title') ||
          logoutButton.getAttribute('aria-label') ||
          window.BookingResourcesI18n.text('logout_hint');
        if (!window.confirm(confirmationMessage)) {
          return;
        }
        window.BookingResourcesApp.logout();
      });
    }

    return {
      reload: loadUserSettings,
    };
  }

  window.BookingResourcesSettings = {
    init,
  };
})();
