window.addEventListener('DOMContentLoaded', async () => {
  await window.BookingResourcesI18n.whenReady();
  await window.BookingResourcesApp.applyPageTitle('reset_password_title', 'Reset password');

  const form = document.getElementById('resetPasswordForm');
  const errorBox = document.getElementById('resetPasswordError');
  const statusBox = document.getElementById('resetPasswordStatus');
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

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    const password = form.elements.new_password.value;
    const confirm = form.elements.confirm_new_password.value;
    if (password !== confirm) {
      showError(text('passwords_do_not_match', 'New passwords do not match.'));
      return;
    }

    try {
      await window.BookingResourcesApp.request('/api/account/password-reset', {
        method: 'POST',
        body: JSON.stringify({ token, new_password: password }),
      });
      errorBox.hidden = true;
      statusBox.textContent = text(
        'password_was_reset',
        'Password was reset. You can sign in now.'
      );
      statusBox.hidden = false;
      form.hidden = true;
    } catch (error) {
      showError(
        (error && error.body && error.body.error) ||
          text('password_reset_failed', 'Failed to reset password.')
      );
    }
  });
});
