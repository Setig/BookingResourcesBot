window.addEventListener('DOMContentLoaded', async () => {
  await window.BookingResourcesI18n.whenReady();
  await window.BookingResourcesApp.applyPageTitle();
  const session = await window.BookingResourcesApp.session();
  if (session.ok) {
    window.BookingResourcesApp.setActiveUserId(session.user_id);
  }
  window.location.href = session.ok ? '/chat.html' : '/authorize.html';
});
