window.addEventListener('DOMContentLoaded', async () => {
  await window.BookingResourcesI18n.whenReady();
  await window.BookingResourcesApp.applyPageTitle('set_password_title', 'Set password');

  const form = document.getElementById('setPasswordForm');
  const loginInput = document.getElementById('assignedLogin');
  const errorBox = document.getElementById('setPasswordError');
  const statusBox = document.getElementById('setPasswordStatus');
  const token = new URLSearchParams(window.location.search).get('token') || '';

  function text(key, fallback) {
    return window.BookingResourcesI18n && typeof window.BookingResourcesI18n.text === 'function'
      ? window.BookingResourcesI18n.text(key) || fallback
      : fallback;
  }

  function showError(message) {
    errorBox.textContent = message;
    errorBox.hidden = !message;
    statusBox.hidden = true;
  }

  if (!token) {
    showError(text('password_link_invalid', 'Password link is invalid or has expired.'));
    form.hidden = true;
    return;
  }

  try {
    const info = await window.BookingResourcesApp.request(
      `/api/account/password-token?token=${encodeURIComponent(token)}`,
      { method: 'GET' }
    );
    if (!info || info.purpose !== 'set_password') {
      showError(text('password_link_invalid', 'Password link is invalid or has expired.'));
    } else {
      loginInput.value = info.login || '';
    }
  } catch (error) {
    showError(
      (error && error.body && error.body.error) ||
        text('password_link_invalid', 'Password link is invalid or has expired.')
    );
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    const password = form.elements.new_password.value;
    const confirm = form.elements.confirm_new_password.value;
    if (password !== confirm) {
      showError(text('passwords_do_not_match', 'New passwords do not match.'));
      return;
    }

    try {
      await window.BookingResourcesApp.request('/api/account/password-set', {
        method: 'POST',
        body: JSON.stringify({ token, new_password: password }),
      });
      errorBox.hidden = true;
      statusBox.textContent = text('password_was_set', 'Password was set. You can sign in now.');
      statusBox.hidden = false;
      form.hidden = true;
    } catch (error) {
      showError(
        (error && error.body && error.body.error) ||
          text('password_set_failed', 'Failed to set password.')
      );
    }
  });
});
