window.addEventListener('DOMContentLoaded', async () => {
  await window.BookingResourcesI18n.whenReady();
  await window.BookingResourcesApp.applyPageTitle('authorize_title', 'Authorization');

  const authMethodSwitch = document.getElementById('authMethodSwitch');
  const webProfileSection = document.getElementById('webProfileSection');
  const localPasswordSection = document.getElementById('localPasswordSection');
  const authorizationForm = document.getElementById('authorizationForm');
  const loginForm = document.getElementById('loginForm');
  const registrationForm = document.getElementById('registrationForm');
  const showWebProfileButton = document.getElementById('showWebProfileButton');
  const showLoginButton = document.getElementById('showLoginButton');
  const showRegistrationButton = document.getElementById('showRegistrationButton');
  const authorizeIntro = document.getElementById('authorizeIntro');
  const authorizedSession = document.getElementById('authorizedSession');
  const authorizedUserId = document.getElementById('authorizedUserId');
  const logoutButton = document.getElementById('authorizeLogoutButton');
  const errorBox = document.getElementById('authorizationError');
  const loginErrorBox = document.getElementById('loginError');
  const registrationErrorBox = document.getElementById('registrationError');
  authorizationForm.elements.locale.value = window.BookingResourcesI18n.currentLocale();
  registrationForm.elements.locale.value = window.BookingResourcesI18n.currentLocale();

  function setAuthMode(mode, focus = false) {
    const signWithoutRegistration = mode === 'sign-without-registration';
    const registering = mode === 'register';
    webProfileSection.hidden = !signWithoutRegistration;
    localPasswordSection.hidden = signWithoutRegistration;
    loginForm.hidden = signWithoutRegistration || registering;
    registrationForm.hidden = signWithoutRegistration || !registering;
    showWebProfileButton.classList.toggle('active', signWithoutRegistration);
    showLoginButton.classList.toggle('active', mode === 'login');
    showRegistrationButton.classList.toggle('active', registering);
    showWebProfileButton.setAttribute('aria-selected', signWithoutRegistration ? 'true' : 'false');
    showLoginButton.setAttribute('aria-selected', mode === 'login' ? 'true' : 'false');
    showRegistrationButton.setAttribute('aria-selected', registering ? 'true' : 'false');
    errorBox.hidden = true;
    loginErrorBox.hidden = true;
    registrationErrorBox.hidden = true;

    if (focus) {
      const activeForm = signWithoutRegistration
        ? authorizationForm
        : registering
          ? registrationForm
          : loginForm;
      const firstInput = activeForm.querySelector('input');
      if (firstInput) {
        firstInput.focus();
      }
    }
  }

  async function completeAuthorization(response, profile, options = {}) {
    const locale = response.locale || profile.locale || window.BookingResourcesI18n.currentLocale();
    await window.BookingResourcesI18n.apply(locale);
    window.BookingResourcesApp.setActiveUserId(response.user_id);
    window.BookingResourcesApp.saveProfile({
      ...profile,
      first_name: response.first_name || profile.first_name || '',
      last_name: response.last_name || profile.last_name || '',
      locale,
      user_id: response.user_id,
    });
    if (options.accessActivationPending) {
      window.BookingResourcesApp.appendHistory(
        window.BookingResourcesApp.accessActivationPendingNoticeMessage(response.user_id)
      );
    }
    window.location.href = '/chat.html';
  }

  const runtimeConfig = await window.BookingResourcesApp.runtimeConfig();
  const authCapabilities = window.BookingResourcesApp.authCapabilities(runtimeConfig);
  const signWithoutRegistrationEnabled = authCapabilities.allowsSignWithoutRegistration;
  const loginEnabled = authCapabilities.allowsLogin;
  const registrationEnabled = authCapabilities.allowsRegistration;
  showWebProfileButton.hidden = !signWithoutRegistrationEnabled;
  showLoginButton.hidden = !loginEnabled;
  showRegistrationButton.hidden = !registrationEnabled;
  const visibleAuthModes =
    (signWithoutRegistrationEnabled ? 1 : 0) + (loginEnabled ? 1 : 0) + (registrationEnabled ? 1 : 0);
  authMethodSwitch.hidden = visibleAuthModes <= 1;
  authorizeIntro.hidden = visibleAuthModes <= 1;
  if (loginEnabled) {
    setAuthMode('login');
  } else if (signWithoutRegistrationEnabled) {
    setAuthMode('sign-without-registration');
  } else {
    webProfileSection.hidden = true;
    localPasswordSection.hidden = true;
  }

  const session = await window.BookingResourcesApp.session();
  if (session.ok) {
    window.BookingResourcesApp.setActiveUserId(session.user_id);
    window.BookingResourcesApp.saveProfile(session);
    await window.BookingResourcesI18n.apply(session.locale || '');
    await window.BookingResourcesApp.applyPageTitle('authorize_title', 'Authorization');
    authorizedUserId.textContent = window.BookingResourcesApp.formatWebUserId(session.user_id);
    authorizeIntro.hidden = true;
    authMethodSwitch.hidden = true;
    webProfileSection.hidden = true;
    localPasswordSection.hidden = true;
    authorizedSession.hidden = false;
  }

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

  if (signWithoutRegistrationEnabled) {
    showWebProfileButton.addEventListener('click', () => {
      setAuthMode('sign-without-registration', true);
    });
  }

  if (loginEnabled) {
    showLoginButton.addEventListener('click', () => {
      setAuthMode('login', true);
    });
  }

  if (registrationEnabled) {
    showRegistrationButton.addEventListener('click', () => {
      setAuthMode('register', true);
    });
  }

  authorizationForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    if (!signWithoutRegistrationEnabled) {
      return;
    }
    errorBox.hidden = true;

    const formData = new FormData(authorizationForm);
    const payload = {
      first_name: String(formData.get('first_name') || '').trim(),
      last_name: String(formData.get('last_name') || '').trim(),
      locale: String(formData.get('locale') || 'en').trim(),
    };

    try {
      const response = await window.BookingResourcesApp.request('/api/authorize', {
        method: 'POST',
        body: JSON.stringify(payload),
      });
      await completeAuthorization(response, payload, { accessActivationPending: true });
    } catch (error) {
      errorBox.textContent =
        error.message || window.BookingResourcesI18n.text('authorization_failed');
      errorBox.hidden = false;
    }
  });

  loginForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    if (!loginEnabled) {
      return;
    }
    loginErrorBox.hidden = true;

    const formData = new FormData(loginForm);
    const payload = {
      login: String(formData.get('login') || '').trim(),
      password: String(formData.get('password') || ''),
    };

    try {
      const response = await window.BookingResourcesApp.request('/api/login', {
        method: 'POST',
        body: JSON.stringify(payload),
      });
      await completeAuthorization(
        response,
        { login: payload.login },
        { accessActivationPending: false }
      );
    } catch (error) {
      loginErrorBox.textContent =
        error.message || window.BookingResourcesI18n.text('authorization_failed');
      loginErrorBox.hidden = false;
    }
  });

  registrationForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    if (!registrationEnabled) {
      return;
    }
    registrationErrorBox.hidden = true;

    const formData = new FormData(registrationForm);
    const password = String(formData.get('password') || '');
    const confirmPassword = String(formData.get('confirm_password') || '');
    if (password !== confirmPassword) {
      registrationErrorBox.textContent = window.BookingResourcesI18n.text('passwords_do_not_match');
      registrationErrorBox.hidden = false;
      return;
    }

    const payload = {
      login: String(formData.get('login') || '').trim(),
      password,
      first_name: String(formData.get('first_name') || '').trim(),
      last_name: String(formData.get('last_name') || '').trim(),
      locale: String(formData.get('locale') || 'en').trim(),
    };

    try {
      const response = await window.BookingResourcesApp.request('/api/register', {
        method: 'POST',
        body: JSON.stringify(payload),
      });
      await completeAuthorization(response, payload, { accessActivationPending: true });
    } catch (error) {
      registrationErrorBox.textContent =
        error.message || window.BookingResourcesI18n.text('authorization_failed');
      registrationErrorBox.hidden = false;
    }
  });
});
