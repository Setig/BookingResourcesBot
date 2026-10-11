(function () {
  const languages = (navigator.languages && navigator.languages.length)
    ? navigator.languages
    : [navigator.language];
  let lang = 'en';
  for (let i = 0; i < languages.length; i += 1) {
    const raw = String(languages[i] || '').toLowerCase().replace(/-/g, '_');
    if (raw.startsWith('ru')) {
      lang = 'ru';
      break;
    }
    if (raw.startsWith('en')) {
      lang = 'en';
      break;
    }
  }
  document.documentElement.lang = lang;
})();
