window.addEventListener('DOMContentLoaded', async () => {
  await window.BookingResourcesI18n.whenReady();

  const session = await window.BookingResourcesApp.session();
  if (!session.ok) {
    window.BookingResourcesApp.redirectToAuthorize();
    return;
  }
  window.BookingResourcesApp.setActiveUserId(session.user_id);
  window.BookingResourcesApp.saveProfile(session);
  try {
    const settings = await window.BookingResourcesApp.request('/api/user-settings', {
      method: 'GET',
    });
    const settingsLocale =
      settings.access_active === false
        ? session.locale || ''
        : ((settings.settings && settings.settings.locale) || '');
    await window.BookingResourcesI18n.apply(settingsLocale);
    window.BookingResourcesApp.saveProfile({ ...session, locale: settingsLocale });
    if (settings.access_active === false) {
      ensureAccessActivationPendingNotice(session.user_id);
    }
  } catch (error) {
    // User settings are optional for users waiting for administrator access activation.
  }
  let baseDocumentTitle = await window.BookingResourcesApp.applyPageTitle('chat_title', 'Chat');

  const messageList = document.getElementById('messageList');
  const messageListFrame = messageList ? messageList.closest('.message-list-frame') : null;
  const form = document.getElementById('messageForm');
  const input = document.getElementById('messageInput');
  const openCommandMenuButton = document.getElementById('openCommandMenuButton');
  const errorBox = document.getElementById('chatError');
  const commandSuggestions = document.getElementById('commandSuggestions');
  const openSettingsButton = document.getElementById('openSettingsButton');
  const settingsPanel = document.getElementById('settingsPanel');
  const settingsCloseButton = document.getElementById('settingsCloseButton');
  const settingsCancelButton = document.getElementById('settingsCancelButton');
  const commandCacheVersion = 2;
  const defaultSuggestedCommandNames = ['/menu', '/start', '/help'];
  const maxCommandSuggestionCount = 20;
  const maxKeyboardColumnCount = 8;
  const eventsCallbackWaitMs = 5000;
  const eventsRetryDelayMs = 1500;
  const eventsIdleDelayMs = 1500;
  const eventsPendingDelayMs = 100;
  let commands = [];
  let commandsLocale = '';
  let cachedCommandUsageFrequencies = null;
  let pendingCallback = null;
  let activeCommandSuggestionIndex = -1;
  let activeCommandSuggestionFromKeyboard = false;
  let keepCommandSuggestionsOnBlur = false;
  let renderedMessageIds = new Set();
  let renderedDayKeys = new Set();
  let hasRenderedMessages = false;
  let sendingBlocked = false;
  let unreadIncomingMessageCount = 0;
  let notificationPermissionPromptArmed = false;
  let dayLabelRefreshTimerId = null;
  let eventsPollTimerId = null;
  let eventsPollInFlight = false;
  let settingsPanelPreviousFocus = null;
  let messageListFadeFrameId = 0;

  function currentCommandLocale() {
    return window.BookingResourcesI18n.currentLocale();
  }

  function currentIntlLocale() {
    return window.BookingResourcesI18n.intlLocale();
  }

  function isSettingsPanelOpen() {
    return Boolean(settingsPanel && !settingsPanel.hidden);
  }

  function openSettingsPanel() {
    if (!settingsPanel) {
      return;
    }
    settingsPanelPreviousFocus = document.activeElement;
    hideCommandSuggestions();
    settingsPanel.hidden = false;
    settingsPanel.scrollTop = 0;
    window.requestAnimationFrame(() => {
      const target = settingsCloseButton || settingsPanel.querySelector('button, input, select');
      if (target) {
        target.focus();
      }
    });
  }

  function closeSettingsPanel() {
    if (!settingsPanel) {
      return;
    }
    settingsPanel.hidden = true;
    const target = settingsPanelPreviousFocus && document.contains(settingsPanelPreviousFocus)
      ? settingsPanelPreviousFocus
      : input;
    settingsPanelPreviousFocus = null;
    if (target && typeof target.focus === 'function') {
      target.focus();
    }
  }

  function isCommandMenuOpen() {
    return !commandSuggestions.hidden && commandSuggestions.dataset.mode === 'menu';
  }

  function setCommandMenuButtonExpanded(expanded) {
    if (!openCommandMenuButton) {
      return;
    }
    openCommandMenuButton.setAttribute('aria-expanded', expanded ? 'true' : 'false');
    openCommandMenuButton.classList.toggle('active', expanded);
  }

  function isCommandSuggestionsRelatedTarget(target) {
    return Boolean(
      target
      && (target === input
        || target === openCommandMenuButton
        || commandSuggestions.contains(target)
        || (openCommandMenuButton && openCommandMenuButton.contains(target)))
    );
  }

  function commandsSupportArgumentCache(loadedCommands) {
    return Array.isArray(loadedCommands)
      && loadedCommands.every((command) =>
        command
        && typeof command === 'object'
        && Array.isArray(command.arguments)
        && Array.isArray(command.argument_completions)
      );
  }

  function loadCommandCache(locale) {
    const key = window.BookingResourcesApp.userScopedKey(
      window.BookingResourcesApp.keys.commandCache
    );
    try {
      const cache = JSON.parse(localStorage.getItem(key) || '{}');
      const entry = cache && cache[locale];
      return entry
        && entry.version === commandCacheVersion
        && commandsSupportArgumentCache(entry.commands)
        ? entry.commands
        : [];
    } catch (error) {
      return [];
    }
  }

  function saveCommandCache(locale, loadedCommands) {
    const key = window.BookingResourcesApp.userScopedKey(
      window.BookingResourcesApp.keys.commandCache
    );
    try {
      const cache = JSON.parse(localStorage.getItem(key) || '{}');
      cache[locale] = {
        version: commandCacheVersion,
        commands: Array.isArray(loadedCommands) ? loadedCommands : [],
        updated_at: new Date().toISOString(),
      };
      localStorage.setItem(key, JSON.stringify(cache));
    } catch (error) {
      window.BookingResourcesApp.clearCommandCache();
    }
  }

  function setComposerVisible(visible) {
    sendingBlocked = !visible;
    form.hidden = !visible;
    if (!visible) {
      hideCommandSuggestions();
    }
    setMessageCommandControlsDisabled(sendingBlocked);
    syncComposerHeight();
    scheduleMessageListFadeStateUpdate();
  }

  function syncComposerHeight() {
    if (!input) {
      return;
    }

    const lineCount = String(input.value || '').split(/\r\n|\r|\n/).length;
    input.rows = Math.min(8, Math.max(1, lineCount));
    scheduleMessageListFadeStateUpdate();
  }

  function refreshComposerSuggestions() {
    if (isCommandMenuOpen()) {
      return;
    }
    renderCommandSuggestions();
  }

  function isSendingBlocked() {
    return sendingBlocked;
  }

  function setMessageCommandControlsDisabled(disabled) {
    messageList.querySelectorAll('.message-command-link').forEach((link) => {
      link.disabled = disabled;
    });
    messageList.querySelectorAll('.message-keyboard .button').forEach((button) => {
      button.disabled = disabled || button.classList.contains('processing');
    });
  }

  function updateMessageListFadeState() {
    if (!messageListFrame) {
      return;
    }

    const maxScrollTop = Math.max(messageList.scrollHeight - messageList.clientHeight, 0);
    const fadeThresholdPx = 2;
    const hasOverflow = maxScrollTop > fadeThresholdPx;
    messageListFrame.classList.toggle(
      'has-top-fade',
      hasOverflow && messageList.scrollTop > fadeThresholdPx
    );
    messageListFrame.classList.toggle(
      'has-bottom-fade',
      hasOverflow && messageList.scrollTop < maxScrollTop - fadeThresholdPx
    );
  }

  function scheduleMessageListFadeStateUpdate() {
    if (messageListFadeFrameId) {
      return;
    }

    messageListFadeFrameId = window.requestAnimationFrame(() => {
      messageListFadeFrameId = 0;
      updateMessageListFadeState();
    });
  }

  function ensureAccessActivationPendingNotice(userId) {
    const notice = window.BookingResourcesApp.accessActivationPendingNoticeMessage(userId);
    const history = window.BookingResourcesApp.loadHistory();
    const existing = history.find((message) => message.id === notice.id);
    if (existing) {
      if (
        existing.text !== notice.text
        || existing.kind !== notice.kind
        || existing.user_id !== notice.user_id
      ) {
        existing.text = notice.text;
        existing.kind = notice.kind;
        existing.user_id = notice.user_id;
        window.BookingResourcesApp.saveHistory(history);
      }
      return;
    }
    window.BookingResourcesApp.appendHistory(notice);
  }

  function shouldHideComposerForRequestError(error) {
    return Boolean(
      error && (error.network || error.serverUnavailable || error.status === 503)
    );
  }

  function eventsFromPayload(payload) {
    if (Array.isArray(payload)) {
      return payload;
    }
    if (payload && Array.isArray(payload.events)) {
      return payload.events;
    }
    return [];
  }

  function isServerAvailableFromPayload(payload) {
    if (Array.isArray(payload)) {
      return true;
    }
    return !payload || payload.server_available !== false;
  }

  function showError(message, hideComposer = Boolean(message)) {
    const hasError = Boolean(message);
    errorBox.textContent = message;
    errorBox.hidden = !hasError;
    setComposerVisible(!hasError || !hideComposer);
  }

  function supportsBrowserNotifications() {
    return 'Notification' in window;
  }

  function isChatActive() {
    return document.visibilityState === 'visible' && document.hasFocus();
  }

  function updateDocumentTitle() {
    if (unreadIncomingMessageCount > 0) {
      document.title = `(${unreadIncomingMessageCount}) ${baseDocumentTitle}`;
      return;
    }
    document.title = baseDocumentTitle;
  }

  function clearIncomingNotifications() {
    if (unreadIncomingMessageCount === 0) {
      return;
    }
    unreadIncomingMessageCount = 0;
    updateDocumentTitle();
  }

  async function requestBrowserNotificationsPermission() {
    if (!supportsBrowserNotifications() || Notification.permission !== 'default') {
      return;
    }

    try {
      const permissionRequest = Notification.requestPermission();
      if (permissionRequest && typeof permissionRequest.then === 'function') {
        await permissionRequest;
      }
    } catch (error) {
      // Browsers can reject permission prompts that are not tied to user activation.
    }
  }

  function armNotificationPermissionPrompt() {
    if (
      notificationPermissionPromptArmed
      || !supportsBrowserNotifications()
      || Notification.permission !== 'default'
    ) {
      return;
    }

    notificationPermissionPromptArmed = true;
    const requestPermissionOnce = () => {
      document.removeEventListener('click', requestPermissionOnce);
      document.removeEventListener('keydown', requestPermissionOnce);
      requestBrowserNotificationsPermission();
    };
    document.addEventListener('click', requestPermissionOnce);
    document.addEventListener('keydown', requestPermissionOnce);
  }

  function plainMessageText(message) {
    const rawText = String((message && message.text) || '').trim();
    if (!rawText) {
      return '';
    }

    const format = Number(message.options && message.options.format);
    if (format === 2) {
      const doc = new DOMParser().parseFromString(rawText, 'text/html');
      return String(doc.body.textContent || '').replace(/\s+/g, ' ').trim();
    }
    return rawText.replace(/\s+/g, ' ').trim();
  }

  function notificationBody(message) {
    const text = plainMessageText(message);
    if (text) {
      return text.length > 160 ? `${text.slice(0, 157)}...` : text;
    }

    if (message && message.type === 'bot_file') {
      const fileName = String(message.file_name || '').trim();
      return fileName || window.BookingResourcesI18n.text('new_chat_file_message');
    }
    return window.BookingResourcesI18n.text('new_chat_message_body');
  }

  function showBrowserNotification(message) {
    if (!supportsBrowserNotifications() || Notification.permission !== 'granted') {
      return;
    }

    try {
      const notification = new Notification(
        window.BookingResourcesI18n.text('new_chat_message_title'),
        {
          body: notificationBody(message),
          tag: message && message.message_id ? `booking-resources-${message.message_id}` : undefined,
        }
      );
      notification.addEventListener('click', () => {
        window.focus();
        clearIncomingNotifications();
        notification.close();
      });
    } catch (error) {
      // Notification construction can fail in unsupported browser contexts.
    }
  }

  function notifyIncomingMessage(message) {
    if (!message || (message.type !== 'bot_message' && message.type !== 'bot_file') || isChatActive()) {
      return;
    }

    unreadIncomingMessageCount += 1;
    updateDocumentTitle();
    showBrowserNotification(message);
  }

  function messageClass(type) {
    if (type === 'user_message') {
      return 'message user';
    }
    if (type === 'meta') {
      return 'message meta';
    }
    return 'message bot';
  }

  function updateMessageStatus(messageId, status) {
    const history = window.BookingResourcesApp.loadHistory();
    const target = history.find((item) => item.id === messageId);
    if (!target) {
      return;
    }
    target.delivery_status = status;
    window.BookingResourcesApp.saveHistory(history);
    cachedCommandUsageFrequencies = null;
    renderMessages();
  }

  function deliveryStatusText(status) {
    if (status === 'delivered') {
      return '✓';
    }
    if (status === 'failed') {
      return '!';
    }
    return '...';
  }

  function deliveryStatusTitle(status) {
    if (status === 'delivered') {
      return window.BookingResourcesI18n.text('delivery_status_delivered');
    }
    if (status === 'failed') {
      return window.BookingResourcesI18n.text('delivery_status_failed');
    }
    return window.BookingResourcesI18n.text('delivery_status_pending');
  }

  function messageDate(message) {
    const date = new Date(message.timestamp || Date.now());
    return Number.isNaN(date.getTime()) ? new Date() : date;
  }

  function dateKey(date) {
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, '0');
    const day = String(date.getDate()).padStart(2, '0');
    return `${year}-${month}-${day}`;
  }

  function previousLocalDay(date) {
    return new Date(date.getFullYear(), date.getMonth(), date.getDate() - 1);
  }

  function millisecondsUntilNextLocalDay(date = new Date()) {
    const nextDay = new Date(date.getFullYear(), date.getMonth(), date.getDate() + 1);
    return Math.max(1000, nextDay.getTime() - date.getTime() + 1000);
  }

  function formatMessageTime(date) {
    return new Intl.DateTimeFormat(currentIntlLocale(), {
      hour: '2-digit',
      minute: '2-digit',
    }).format(date);
  }

  function formatFullDateTime(date) {
    return new Intl.DateTimeFormat(currentIntlLocale(), {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    }).format(date);
  }

  function formatDayLabel(date) {
    const now = new Date();
    if (dateKey(date) === dateKey(now)) {
      return window.BookingResourcesI18n.text('day_today', 'Today');
    }
    if (dateKey(date) === dateKey(previousLocalDay(now))) {
      return window.BookingResourcesI18n.text('day_yesterday', 'Yesterday');
    }
    return new Intl.DateTimeFormat(currentIntlLocale(), {
      weekday: 'long',
      year: 'numeric',
      month: 'long',
      day: 'numeric',
    }).format(date);
  }

  function scrollToDayGroupStart(dayGroup) {
    if (!messageList || !dayGroup || dayGroup.parentElement !== messageList) {
      return;
    }
    const firstMessage = dayGroup.querySelector('.message');
    const separator = dayGroup.querySelector('.message-day-separator');
    if (!firstMessage || !separator) {
      messageList.scrollTo({
        top: Math.max(dayGroup.offsetTop, 0),
        behavior: 'smooth',
      });
      return;
    }
    const separatorGap = parseFloat(window.getComputedStyle(separator).marginBottom) || 0;
    messageList.scrollTo({
      top: Math.max(dayGroup.offsetTop + firstMessage.offsetTop - separator.offsetHeight - separatorGap, 0),
      behavior: 'smooth',
    });
  }

  function appendDaySeparator(parent, date, animate) {
    const separator = document.createElement('button');
    separator.type = 'button';
    separator.className = 'message-day-separator';
    separator.dataset.timestamp = date.toISOString();
    if (animate) {
      separator.classList.add('message-enter');
    }
    separator.textContent = formatDayLabel(date);
    separator.addEventListener('click', () => {
      scrollToDayGroupStart(parent);
    });
    parent.appendChild(separator);
  }

  function createDayGroup(date, animate) {
    const group = document.createElement('section');
    group.className = 'message-day-group';
    appendDaySeparator(group, date, animate);
    return group;
  }

  function refreshDaySeparatorLabels() {
    messageList.querySelectorAll('.message-day-separator').forEach((separator) => {
      const date = new Date(separator.dataset.timestamp || '');
      if (!Number.isNaN(date.getTime())) {
        separator.textContent = formatDayLabel(date);
      }
    });
  }

  function scheduleDayLabelRefresh() {
    if (dayLabelRefreshTimerId !== null) {
      window.clearTimeout(dayLabelRefreshTimerId);
    }
    dayLabelRefreshTimerId = window.setTimeout(() => {
      dayLabelRefreshTimerId = null;
      refreshDaySeparatorLabels();
      scheduleDayLabelRefresh();
    }, millisecondsUntilNextLocalDay());
  }

  function renderMessageFooter(parent, message, date) {
    const meta = document.createElement('div');
    meta.className = 'message-meta-info';

    const time = document.createElement('time');
    time.className = 'message-time';
    time.dateTime = date.toISOString();
    time.title = formatFullDateTime(date);
    time.textContent = formatMessageTime(date);
    meta.appendChild(time);

    if (message.type === 'user_message') {
      const deliveryStatus = message.delivery_status || 'pending';
      const deliveryTitle = deliveryStatusTitle(deliveryStatus);
      const status = document.createElement('span');
      status.className = `delivery-status ${deliveryStatus}`;
      status.title = deliveryTitle;
      status.setAttribute('aria-label', deliveryTitle);
      status.textContent = deliveryStatusText(deliveryStatus);
      meta.appendChild(status);
    }

    parent.appendChild(meta);
  }

  function commandName(command) {
    if (typeof command === 'string') {
      return command.trim();
    }
    return String((command && command.name) || '').trim();
  }

  function commandUsage(command) {
    if (typeof command === 'string') {
      return command.trim();
    }
    return String((command && command.usage) || commandName(command)).trim();
  }

  function commandHelpText(command) {
    return String((command && command.help) || (command && command.description) || '').trim();
  }

  function commandSuggestionTitle(command, descriptionText = commandHelpText(command)) {
    const usage = commandUsage(command) || commandName(command);
    const description = String(descriptionText || '').trim();
    const parts = [];
    if (usage) {
      parts.push(usage);
    }
    if (description && description !== usage) {
      parts.push(description);
    }
    return parts.join('\n');
  }

  function commandHasArguments(command) {
    if (!command || typeof command === 'string') {
      return false;
    }
    if (Object.prototype.hasOwnProperty.call(command, 'has_arguments')) {
      return Boolean(command.has_arguments);
    }

    const name = commandName(command);
    const usage = commandUsage(command);
    return usage.length > name.length && usage.toLowerCase().startsWith(name.toLowerCase());
  }

  function invalidateCommandUsageFrequencies() {
    cachedCommandUsageFrequencies = null;
  }

  function commandUsageFrequencies() {
    if (cachedCommandUsageFrequencies !== null) {
      return cachedCommandUsageFrequencies;
    }

    const history = window.BookingResourcesApp.loadHistory();
    const frequencies = new Map();
    const commandPattern = /(^|\n)\s*(\/[A-Za-z0-9_]+)/g;

    history.forEach((message) => {
      if (!message || message.type !== 'user_message' || message.delivery_status === 'failed') {
        return;
      }

      const text = String(message.text || '');
      let match = commandPattern.exec(text);
      while (match) {
        const name = String(match[2] || '').trim().toLowerCase();
        if (name) {
          frequencies.set(name, (frequencies.get(name) || 0) + 1);
        }
        match = commandPattern.exec(text);
      }
    });

    cachedCommandUsageFrequencies = frequencies;
    return cachedCommandUsageFrequencies;
  }

  function defaultSuggestedCommandPriority(name) {
    const normalizedName = String(name || '').trim().toLowerCase();
    const index = defaultSuggestedCommandNames.indexOf(normalizedName);
    return index >= 0 ? index : defaultSuggestedCommandNames.length;
  }

  function orderedCommands(sourceCommands) {
    const indexedCommands = Array.isArray(sourceCommands)
      ? sourceCommands.map((command, index) => ({ command, index }))
      : [];
    const frequencies = commandUsageFrequencies();
    const hasUserFrequencies = Array.from(frequencies.values()).some((count) => count > 0);

    indexedCommands.sort((lhs, rhs) => {
      const lhsName = commandName(lhs.command).toLowerCase();
      const rhsName = commandName(rhs.command).toLowerCase();
      const lhsFrequency = frequencies.get(lhsName) || 0;
      const rhsFrequency = frequencies.get(rhsName) || 0;

      if (lhsFrequency !== rhsFrequency) {
        return rhsFrequency - lhsFrequency;
      }

      if (!hasUserFrequencies) {
        const lhsPriority = defaultSuggestedCommandPriority(lhsName);
        const rhsPriority = defaultSuggestedCommandPriority(rhsName);
        if (lhsPriority !== rhsPriority) {
          return lhsPriority - rhsPriority;
        }
      }

      return lhs.index - rhs.index;
    });

    return indexedCommands.map(({ command }) => command);
  }

  function commandMap() {
    const result = new Map();
    commands.forEach((command) => {
      const name = commandName(command).toLowerCase();
      if (name) {
        result.set(name, command);
      }
    });
    return result;
  }

  function commandSegmentAtCursor(value, cursor) {
    const text = String(value || '');
    const safeCursor = Math.max(0, Math.min(Number(cursor) || 0, text.length));
    let rawStart = 0;
    let inQuotes = false;
    let escape = false;
    let quoteChar = '';
    let segment = null;

    const captureSegment = (rawEnd) => {
      if (safeCursor < rawStart || safeCursor > rawEnd) {
        return;
      }

      let start = rawStart;
      while (start < rawEnd && /\s/.test(text.charAt(start))) {
        ++start;
      }

      let end = rawEnd;
      while (end > start && /\s/.test(text.charAt(end - 1))) {
        --end;
      }

      segment = {
        rawStart,
        rawEnd,
        start,
        end,
        text: text.slice(start, end),
        cursor: safeCursor,
      };
    };

    for (let index = 0; index < text.length; ++index) {
      const ch = text.charAt(index);
      if (escape) {
        if (ch === '\r' && text.charAt(index + 1) === '\n') {
          ++index;
        }
        escape = false;
        continue;
      }
      if (ch === '\\') {
        escape = true;
        continue;
      }
      if (inQuotes) {
        if (ch === quoteChar) {
          inQuotes = false;
        }
        continue;
      }
      if (ch === '"' || ch === '\'') {
        inQuotes = true;
        quoteChar = ch;
        continue;
      }
      if (ch === '\r' || ch === '\n') {
        captureSegment(index);
        if (segment) {
          return segment.text ? segment : null;
        }
        if (ch === '\r' && text.charAt(index + 1) === '\n') {
          ++index;
        }
        rawStart = index + 1;
      }
    }

    captureSegment(text.length);
    return segment && segment.text ? segment : null;
  }

  function lineBoundsAtCursor(value, cursor) {
    const text = String(value || '');
    const safeCursor = Math.max(0, Math.min(Number(cursor) || 0, text.length));
    let start = safeCursor;
    while (start > 0) {
      const ch = text.charAt(start - 1);
      if (ch === '\n' || ch === '\r') {
        break;
      }
      --start;
    }

    let rawEnd = safeCursor;
    while (rawEnd < text.length) {
      const ch = text.charAt(rawEnd);
      if (ch === '\n' || ch === '\r') {
        break;
      }
      ++rawEnd;
    }

    return { start, rawEnd };
  }

  function commandByInputToken(value, cursor = String(value || '').length) {
    const segment = commandSegmentAtCursor(value, cursor);
    if (!segment || segment.cursor < segment.start || !segment.text.startsWith('/')) {
      return null;
    }

    const token = segment.text.split(/\s+/, 1)[0].toLowerCase();
    return commandMap().get(token) || null;
  }

  function commandForToken(token, knownCommands) {
    const normalized = String(token || '').trim().toLowerCase();
    if (!normalized.startsWith('/')) {
      return null;
    }
    if (knownCommands.size === 0) {
      return token;
    }
    return knownCommands.get(normalized) || null;
  }

  function isDashSeparatorStart(text) {
    const trimmed = String(text || '').trimStart();
    const firstCharCode = trimmed.charCodeAt(0);
    return trimmed.startsWith('-') || firstCharCode === 0x2013 || firstCharCode === 0x2014;
  }

  function isCommandStart(text, index) {
    if (index === 0) {
      return true;
    }
    const previous = text.charAt(index - 1);
    return /\s/.test(previous) || '([{<"\''.includes(previous);
  }

  function isUrlStart(text, index) {
    if (index === 0) {
      return true;
    }
    const previous = text.charAt(index - 1);
    return /\s/.test(previous) || '([{<"\'«'.includes(previous);
  }

  function rangeOverlaps(start, end, ranges) {
    return ranges.some((range) => start < range.end && end > range.start);
  }

  function clampRangeEnd(start, end, occupiedRanges) {
    let limit = end;
    occupiedRanges.forEach((range) => {
      if (range.start >= start && range.start < limit) {
        limit = range.start;
      }
    });
    return limit;
  }

  function trimAutolinkMatch(raw) {
    let text = String(raw || '');
    while (text.length > 0) {
      const ch = text.charAt(text.length - 1);
      if ('.,;:!?»'.includes(ch)) {
        text = text.slice(0, -1);
        continue;
      }
      if (ch === ')' || ch === ']' || ch === '}') {
        const open = ch === ')' ? '(' : ch === ']' ? '[' : '{';
        let openCount = 0;
        let closeCount = 0;
        for (let i = 0; i < text.length; ++i) {
          const current = text.charAt(i);
          if (current === open) {
            ++openCount;
          } else if (current === ch) {
            ++closeCount;
          }
        }
        if (closeCount > openCount) {
          text = text.slice(0, -1);
          continue;
        }
      }
      break;
    }
    return text;
  }

  function safeAbsoluteLinkUrl(value, options) {
    try {
      const url = new URL(value);
      if (url.protocol !== 'http:' && url.protocol !== 'https:' && url.protocol !== 'mailto:') {
        return '';
      }
      if (url.protocol === 'mailto:') {
        return url.pathname ? url.href : '';
      }
      const host = String(url.hostname || '').replace(/\.$/, '');
      if (!host) {
        return '';
      }
      if (options && options.requireDotInHost && !host.includes('.')) {
        return '';
      }
      return url.href;
    } catch (error) {
      // Ignore invalid URLs and keep the original text.
    }
    return '';
  }

  function autolinkHref(matchedText) {
    const isWww = /^www\./i.test(matchedText);
    const candidate = isWww ? `https://${matchedText}` : matchedText;
    return safeAbsoluteLinkUrl(candidate, { requireDotInHost: isWww });
  }

  function collectAutolinkSpans(text) {
    const spans = [];
    const urlRegex = /(?:https?:\/\/|www\.|mailto:)[^\s<>"'`]+/gi;
    let match = urlRegex.exec(text);
    while (match) {
      const start = match.index;
      if (!isUrlStart(text, start)) {
        urlRegex.lastIndex = start + 1;
        match = urlRegex.exec(text);
        continue;
      }

      const visible = trimAutolinkMatch(match[0]);
      const href = visible ? autolinkHref(visible) : '';
      if (!href) {
        urlRegex.lastIndex = start + 1;
        match = urlRegex.exec(text);
        continue;
      }

      const end = start + visible.length;
      spans.push({
        type: 'url',
        start,
        end,
        text: visible,
        href,
      });
      urlRegex.lastIndex = end;
      match = urlRegex.exec(text);
    }
    return spans;
  }

  function isLineCommandStart(text, index) {
    for (let cursor = index - 1; cursor >= 0; --cursor) {
      const ch = text.charAt(cursor);
      if (ch === '\n' || ch === '\r') {
        return true;
      }
      if (!/\s/.test(ch)) {
        return false;
      }
    }
    return true;
  }

  function lineEndIndex(text, index) {
    const newlineIndex = text.indexOf('\n', index);
    const carriageIndex = text.indexOf('\r', index);
    if (newlineIndex < 0) {
      return carriageIndex < 0 ? text.length : carriageIndex;
    }
    if (carriageIndex < 0) {
      return newlineIndex;
    }
    return Math.min(newlineIndex, carriageIndex);
  }

  function placeholderEnd(text, index) {
    const open = text.charAt(index);
    const close = open === '<' ? '>' : open === '[' ? ']' : '';
    if (!close) {
      return index;
    }

    for (let cursor = index + 1; cursor < text.length; ++cursor) {
      const ch = text.charAt(cursor);
      if (ch === '\n' || ch === '\r') {
        return index;
      }
      if (ch === close) {
        return cursor + 1;
      }
    }
    return index;
  }

  function usagePlaceholderTail(text, index) {
    let cursor = index;
    let end = index;
    while (cursor < text.length) {
      const whitespaceStart = cursor;
      while (cursor < text.length && /\s/.test(text.charAt(cursor))) {
        if (text.charAt(cursor) === '\n' || text.charAt(cursor) === '\r') {
          return text.slice(index, end);
        }
        ++cursor;
      }
      if (cursor === whitespaceStart) {
        return text.slice(index, end);
      }

      const tokenEnd = placeholderEnd(text, cursor);
      if (tokenEnd === cursor) {
        return text.slice(index, end);
      }

      cursor = tokenEnd;
      end = cursor;
    }
    return text.slice(index, end);
  }

  function commandHelpLineFallback(text, start, commandEnd) {
    if (!isLineCommandStart(text, start)) {
      return null;
    }

    const placeholderTail = usagePlaceholderTail(text, commandEnd);
    const separatorTail = text.slice(
      commandEnd + placeholderTail.length,
      lineEndIndex(text, commandEnd)
    );
    return isDashSeparatorStart(separatorTail) ? text.slice(start, commandEnd) : null;
  }

  function commandLineArgumentTail(text, commandEnd) {
    const tail = text.slice(commandEnd, lineEndIndex(text, commandEnd)).trimEnd();
    const trimmedTail = tail.trimStart();
    if (!trimmedTail || isDashSeparatorStart(trimmedTail)) {
      return '';
    }
    return tail;
  }

  function commandExpressionText(text, start, commandEnd, command) {
    const usage = commandUsage(command);
    if (usage.length > commandName(command).length && text.slice(start, start + usage.length) === usage) {
      return usage;
    }

    const placeholderTail = usagePlaceholderTail(text, commandEnd);
    if (placeholderTail) {
      return text.slice(start, commandEnd) + placeholderTail;
    }

    if (isLineCommandStart(text, start)) {
      const argumentTail = commandLineArgumentTail(text, commandEnd);
      if (argumentTail) {
        return text.slice(start, commandEnd) + argumentTail;
      }
    }

    return text.slice(start, commandEnd);
  }

  function commandInputText(commandText, command) {
    const trimmed = String(commandText || '').trim();
    if (!commandHasArguments(command)) {
      return trimmed;
    }

    const name = commandName(command);
    const usage = commandUsage(command);
    if (trimmed.toLowerCase() === name.toLowerCase()) {
      return usage.length > name.length ? usage : `${trimmed} `;
    }
    return trimmed || usage || name;
  }

  function appendCommandLink(parent, commandText, command, opensComposer) {
    const link = document.createElement('button');
    link.type = 'button';
    link.className = 'message-command-link';
    link.textContent = commandText;
    link.disabled = isSendingBlocked();
    link.addEventListener('click', () => {
      if (isSendingBlocked()) {
        return;
      }
      hideCommandSuggestions();
      if (opensComposer || commandHasArguments(command)) {
        input.value = commandInputText(commandText, command);
        input.focus();
        input.setSelectionRange(input.value.length, input.value.length);
        syncComposerHeight();
        return;
      }
      sendText(commandName(command) || commandText);
    });
    parent.appendChild(link);
  }

  function appendAutolink(parent, visibleText, href) {
    const link = document.createElement('a');
    link.setAttribute('href', href);
    link.setAttribute('target', '_blank');
    link.setAttribute('rel', 'noopener noreferrer');
    link.textContent = visibleText;
    parent.appendChild(link);
  }

  function collectCommandSpans(text, occupiedRanges) {
    const knownCommands = commandMap();
    const commandRegex = /\/[A-Za-z0-9_]+/g;
    let match = commandRegex.exec(text);

    while (match) {
      const commandText = match[0];
      const start = match.index;
      const tokenEnd = start + commandText.length;
      const knownCommand = commandForToken(commandText, knownCommands);
      const command = knownCommand || commandHelpLineFallback(text, start, tokenEnd);
      if (isCommandStart(text, start) && command && !rangeOverlaps(start, tokenEnd, occupiedRanges)) {
        const requestedText = commandExpressionText(text, start, tokenEnd, command);
        let end = clampRangeEnd(start, start + requestedText.length, occupiedRanges);
        let linkText = text.slice(start, end);
        if (end < start + requestedText.length) {
          linkText = linkText.trimEnd();
          end = start + linkText.length;
        }
        if (end > start && !rangeOverlaps(start, end, occupiedRanges)) {
          const opensComposer =
            commandHasArguments(command)
            || linkText.trim().toLowerCase() !== commandText.trim().toLowerCase();
          occupiedRanges.push({
            type: 'command',
            start,
            end,
            text: linkText,
            command,
            opensComposer,
          });
          commandRegex.lastIndex = end;
        }
      }
      match = commandRegex.exec(text);
    }
  }

  function appendTextWithInlineLinks(parent, rawText, options) {
    const text = String(rawText || '');
    const parseCommands = !options || options.parseCommands !== false;
    const parseUrls = !options || options.parseUrls !== false;
    const spans = parseUrls ? collectAutolinkSpans(text) : [];
    if (parseCommands) {
      collectCommandSpans(text, spans);
    }
    spans.sort((left, right) => left.start - right.start);

    let cursor = 0;
    spans.forEach((span) => {
      if (span.start > cursor) {
        parent.appendChild(document.createTextNode(text.slice(cursor, span.start)));
      }
      if (span.type === 'url') {
        appendAutolink(parent, span.text, span.href);
      } else {
        appendCommandLink(parent, span.text, span.command, span.opensComposer);
      }
      cursor = span.end;
    });

    if (cursor < text.length) {
      parent.appendChild(document.createTextNode(text.slice(cursor)));
    }
  }

  function safeLinkUrl(value) {
    try {
      const url = new URL(value, window.location.href);
      if (url.protocol === 'http:' || url.protocol === 'https:' || url.protocol === 'mailto:') {
        return url.href;
      }
    } catch (error) {
      // Ignore invalid URLs and render link text without href.
    }
    return '';
  }

  function appendSanitizedHtml(parent, rawHtml) {
    const allowedTags = new Set([
      'a',
      'b',
      'br',
      'code',
      'del',
      'em',
      'i',
      'ins',
      'pre',
      's',
      'span',
      'strike',
      'strong',
      'u',
    ]);
    const parser = new DOMParser();
    const doc = parser.parseFromString(String(rawHtml || ''), 'text/html');

    function copyChildren(source, target, parseOptions) {
      source.childNodes.forEach((child) => copyNode(child, target, parseOptions));
    }

    function copyNode(source, target, parseOptions) {
      const parseCommands = Boolean(parseOptions && parseOptions.parseCommands);
      const parseUrls = Boolean(parseOptions && parseOptions.parseUrls);
      if (source.nodeType === Node.TEXT_NODE) {
        if (parseCommands || parseUrls) {
          appendTextWithInlineLinks(target, source.textContent || '', {
            parseCommands,
            parseUrls,
          });
        } else {
          target.appendChild(document.createTextNode(source.textContent || ''));
        }
        return;
      }

      if (source.nodeType !== Node.ELEMENT_NODE) {
        return;
      }

      const tagName = source.tagName.toLowerCase();
      if (!allowedTags.has(tagName)) {
        copyChildren(source, target, parseOptions);
        return;
      }

      if (tagName === 'br') {
        target.appendChild(document.createElement('br'));
        return;
      }

      const element = document.createElement(tagName);
      if (tagName === 'a') {
        const href = safeLinkUrl(source.getAttribute('href') || '');
        if (href) {
          element.setAttribute('href', href);
          element.setAttribute('target', '_blank');
          element.setAttribute('rel', 'noopener noreferrer');
        }
      }

      const allowInline = tagName !== 'code' && tagName !== 'pre';
      copyChildren(source, element, {
        parseCommands: parseCommands && allowInline,
        parseUrls: parseUrls && allowInline && tagName !== 'a',
      });
      target.appendChild(element);
    }

    copyChildren(doc.body, parent, { parseCommands: true, parseUrls: true });
  }

  function appendMarkdownInline(parent, rawText) {
    const text = String(rawText || '');
    let cursor = 0;

    while (cursor < text.length) {
      if (text.startsWith('```', cursor)) {
        const end = text.indexOf('```', cursor + 3);
        if (end >= 0) {
          const pre = document.createElement('pre');
          const code = document.createElement('code');
          code.textContent = text.slice(cursor + 3, end);
          pre.appendChild(code);
          parent.appendChild(pre);
          cursor = end + 3;
          continue;
        }
      }

      const marker = text.charAt(cursor);
      if (marker === '`' || marker === '*' || marker === '_') {
        const end = text.indexOf(marker, cursor + 1);
        if (end > cursor + 1) {
          const content = text.slice(cursor + 1, end);
          const element =
            marker === '`'
              ? document.createElement('code')
              : document.createElement(marker === '*' ? 'strong' : 'em');
          if (marker === '`') {
            element.textContent = content;
          } else {
            appendMarkdownInline(element, content);
          }
          parent.appendChild(element);
          cursor = end + 1;
          continue;
        }
      }

      const nextSpecials = ['```', '`', '*', '_']
        .map((token) => text.indexOf(token, cursor + 1))
        .filter((index) => index >= 0);
      const next = nextSpecials.length > 0 ? Math.min(...nextSpecials) : text.length;
      appendTextWithInlineLinks(parent, text.slice(cursor, next));
      cursor = next;
    }
  }

  function isAccessActivationPendingNotice(message) {
    return Boolean(message)
      && (message.kind === 'access_activation_pending'
        || String(message.id || '').startsWith('access-activation-pending-'));
  }

  function renderAccessActivationPendingNotice(parent, message) {
    const userId = message.user_id
      || String(message.id || '').slice('access-activation-pending-'.length);
    const parts = window.BookingResourcesApp.accessActivationPendingNoticeParts(userId);
    const notice = document.createElement('span');
    notice.className = 'access-activation-pending-text';
    notice.textContent = parts.notice;
    const userIdLine = document.createElement('span');
    userIdLine.className = 'access-activation-user-id';
    const label = document.createElement('span');
    label.className = 'access-activation-user-id-label';
    label.textContent = parts.label;
    const value = document.createElement('span');
    value.className = 'user-id-value';
    value.textContent = parts.userId;
    userIdLine.appendChild(label);
    userIdLine.appendChild(value);
    parent.appendChild(notice);
    parent.appendChild(userIdLine);
  }

  function renderMessageText(parent, message) {
    if (isAccessActivationPendingNotice(message)) {
      renderAccessActivationPendingNotice(parent, message);
      return;
    }
    const rawText = message.text == null ? '' : String(message.text);
    if (!rawText) {
      return;
    }
    const format = Number(message.options && message.options.format);
    if (format === 2) {
      appendSanitizedHtml(parent, rawText);
      return;
    }
    if (format === 1) {
      appendMarkdownInline(parent, rawText);
      return;
    }
    appendTextWithInlineLinks(parent, rawText);
  }

  function hasFileAttachment(message) {
    return message.type === 'bot_file'
      && Boolean(message.file_id)
      && message.file_available !== false;
  }

  function formatFileSize(size) {
    let value = Number(size || 0);
    if (!Number.isFinite(value) || value <= 0) {
      return '';
    }

    const units = ['B', 'KB', 'MB', 'GB'];
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
      value /= 1024;
      unitIndex += 1;
    }

    const maximumFractionDigits = unitIndex === 0 ? 0 : 1;
    const formatted = new Intl.NumberFormat(currentIntlLocale(), {
      maximumFractionDigits,
    }).format(value);
    return `${formatted} ${units[unitIndex]}`;
  }

  function fileNameParts(fileName) {
    const characters = Array.from(String(fileName || ''));
    if (characters.length <= 32) {
      return { start: characters.join(''), end: '' };
    }

    const endLength = Math.min(18, Math.max(10, Math.floor(characters.length * 0.35)));
    return {
      start: characters.slice(0, characters.length - endLength).join(''),
      end: characters.slice(-endLength).join(''),
    };
  }

  function renderFileAttachment(parent, message) {
    if (message.type !== 'bot_file') {
      return;
    }

    const attachment = document.createElement('div');
    attachment.className = 'message-file';

    if (!hasFileAttachment(message)) {
      attachment.classList.add('unavailable');
      attachment.textContent = window.BookingResourcesI18n.text('file_not_available');
      parent.appendChild(attachment);
      return;
    }

    const fileName = String(message.file_name || '').trim()
      || window.BookingResourcesI18n.text('download_file');
    const link = document.createElement('a');
    link.className = 'message-file-link';
    link.href = `/api/files/${encodeURIComponent(message.file_id)}`;
    link.title = fileName;

    const icon = document.createElement('span');
    icon.className = 'message-file-icon';
    icon.textContent = '📄';
    link.appendChild(icon);

    const info = document.createElement('span');
    info.className = 'message-file-info';

    const name = document.createElement('span');
    name.className = 'message-file-name';
    const parts = fileNameParts(fileName);
    const nameStart = document.createElement('span');
    nameStart.className = 'message-file-name-start';
    nameStart.textContent = parts.start;
    name.appendChild(nameStart);
    if (parts.end) {
      const nameEnd = document.createElement('span');
      nameEnd.className = 'message-file-name-end';
      nameEnd.textContent = parts.end;
      name.appendChild(nameEnd);
    }
    info.appendChild(name);

    const size = formatFileSize(message.file_size);
    if (size) {
      const sizeElement = document.createElement('span');
      sizeElement.className = 'message-file-size';
      sizeElement.textContent = size;
      info.appendChild(sizeElement);
    }

    link.appendChild(info);
    attachment.appendChild(link);
    parent.appendChild(attachment);
  }

  function isButtonProcessing(message, button) {
    if (!pendingCallback) {
      return false;
    }
    return pendingCallback.messageId === message.message_id
      && pendingCallback.callbackData === (button.callback_data || '');
  }

  function clearPendingCallbackForEvent(event) {
    if (!pendingCallback) {
      return;
    }
    if (event.type === 'bot_edit' && event.message_id === pendingCallback.messageId) {
      pendingCallback = null;
      return;
    }
    if (event.type === 'bot_message' || event.type === 'bot_file') {
      pendingCallback = null;
    }
  }

  function findEditableHistoryMessage(history, event) {
    if (pendingCallback && pendingCallback.messageId === event.message_id && pendingCallback.historyId) {
      const targetByHistoryId = history.find((item) => item.id === pendingCallback.historyId);
      if (targetByHistoryId) {
        return targetByHistoryId;
      }
    }

    for (let index = history.length - 1; index >= 0; --index) {
      if (history[index].message_id === event.message_id) {
        return history[index];
      }
    }
    return null;
  }

  function renderKeyboard(parent, message) {
    const options = message.options || {};
    const rows = options && options.inline_keyboard ? options.inline_keyboard : [];
    if (rows.length === 0) {
      return;
    }

    const keyboard = document.createElement('div');
    keyboard.className = 'message-keyboard';
    rows.forEach((row) => {
      const rowElement = document.createElement('div');
      const keyboardColumnCount = Math.min(Math.max(row.length, 1), maxKeyboardColumnCount);
      rowElement.className = `keyboard-row keyboard-row-columns-${keyboardColumnCount}`;
      row.forEach((button) => {
        const item = document.createElement('button');
        item.type = 'button';
        item.className = 'button secondary';
        item.textContent = button.text || '';
        if (isButtonProcessing(message, button)) {
          item.classList.add('processing');
          item.disabled = true;
        }
        if (isSendingBlocked()) {
          item.disabled = true;
        }
        item.addEventListener('click', async () => {
          if (isSendingBlocked()) {
            return;
          }
          pendingCallback = {
            historyId: message.id || '',
            messageId: message.message_id || '',
            callbackData: button.callback_data || '',
          };
          item.classList.add('processing');
          item.disabled = true;
          try {
            await window.BookingResourcesApp.request('/api/callbacks', {
              method: 'POST',
              body: JSON.stringify({
                data: button.callback_data || '',
                original_message_id: message.message_id || '',
              }),
            });
            schedulePollEvents(0);
          } catch (error) {
            pendingCallback = null;
            renderMessages();
            showError(
              error.message || window.BookingResourcesI18n.text('connection_lost'),
              shouldHideComposerForRequestError(error)
            );
          }
        });
        rowElement.appendChild(item);
      });
      keyboard.appendChild(rowElement);
    });
    parent.appendChild(keyboard);
  }

  function commandQuery(value, cursor = String(value || '').length) {
    const segment = commandSegmentAtCursor(value, cursor);
    if (!segment || segment.cursor < segment.start || !segment.text.startsWith('/')) {
      return null;
    }
    const token = segment.text.split(/\s+/, 1)[0].slice(1).toLowerCase();
    return token;
  }

  function hideCommandSuggestions() {
    commandSuggestions.hidden = true;
    commandSuggestions.innerHTML = '';
    commandSuggestions.dataset.mode = '';
    activeCommandSuggestionIndex = -1;
    activeCommandSuggestionFromKeyboard = false;
    setCommandMenuButtonExpanded(false);
  }

  function commandSuggestionItems() {
    return Array.from(commandSuggestions.querySelectorAll('.command-suggestion'));
  }

  function setActiveCommandSuggestion(index, fromKeyboard = false) {
    const items = commandSuggestionItems();
    activeCommandSuggestionIndex = items.length === 0
      ? -1
      : Math.min(items.length - 1, Math.max(0, index));
    activeCommandSuggestionFromKeyboard = activeCommandSuggestionIndex >= 0 && fromKeyboard;
    items.forEach((item, itemIndex) => {
      item.classList.toggle('active', itemIndex === activeCommandSuggestionIndex);
    });
    if (activeCommandSuggestionIndex >= 0) {
      items[activeCommandSuggestionIndex].scrollIntoView({ block: 'nearest' });
    }
  }

  function canNavigateCommandSuggestions() {
    if (commandSuggestions.hidden) {
      return false;
    }

    const items = commandSuggestionItems();
    if (items.length === 0) {
      return false;
    }

    return !(
      commandSuggestions.dataset.mode === 'command'
      && items.length === 1
      && items[0].classList.contains('command-detail')
    );
  }

  function moveActiveCommandSuggestion(delta) {
    const items = commandSuggestionItems();
    if (!canNavigateCommandSuggestions() || items.length === 0) {
      return false;
    }

    const nextIndex = activeCommandSuggestionIndex < 0
      ? (delta > 0 ? 0 : items.length - 1)
      : (activeCommandSuggestionIndex + delta + items.length) % items.length;
    setActiveCommandSuggestion(nextIndex, true);
    return true;
  }

  function chooseActiveCommandSuggestion() {
    const items = commandSuggestionItems();
    if (
      commandSuggestions.hidden
      || !activeCommandSuggestionFromKeyboard
      || activeCommandSuggestionIndex < 0
      || activeCommandSuggestionIndex >= items.length
    ) {
      return false;
    }

    items[activeCommandSuggestionIndex].click();
    return true;
  }

  function chooseVisibleCommandSuggestion() {
    const items = commandSuggestionItems();
    if (commandSuggestions.hidden || items.length === 0) {
      return false;
    }

    const index = activeCommandSuggestionIndex >= 0 ? activeCommandSuggestionIndex : 0;
    items[Math.min(index, items.length - 1)].click();
    return true;
  }

  function applyCommandSuggestion(command, preserveInput = false) {
    const segment = commandSegmentAtCursor(input.value, input.selectionStart);
    const nameText = commandName(command);
    const inputText = preserveInput && segment ? segment.text : nameText;
    const replacement = commandHasArguments(command)
      ? commandInputText(inputText || nameText, command)
      : `${nameText} `;
    const bounds = segment || lineBoundsAtCursor(input.value, input.selectionStart);
    input.value = `${input.value.slice(0, bounds.start)}${replacement}${input.value.slice(bounds.rawEnd)}`;
    const cursor = bounds.start + replacement.length;
    input.focus();
    input.setSelectionRange(cursor, cursor);
    syncComposerHeight();
    renderCommandSuggestions();
  }

  function commonPrefix(values) {
    if (!Array.isArray(values) || values.length === 0) {
      return '';
    }

    let prefix = String(values[0] || '');
    values.slice(1).forEach((value) => {
      const text = String(value || '');
      let index = 0;
      while (
        index < prefix.length
        && index < text.length
        && prefix.charAt(index).toLowerCase() === text.charAt(index).toLowerCase()
      ) {
        ++index;
      }
      prefix = prefix.slice(0, index);
    });
    return prefix;
  }

  function commandTokenInfo(value, cursor) {
    const text = String(value || '');
    const segment = commandSegmentAtCursor(text, cursor);
    if (!segment || segment.cursor < segment.start || segment.start >= segment.end) {
      return null;
    }
    const start = segment.start;
    if (text.charAt(start) !== '/') {
      return null;
    }

    let end = start;
    while (end < segment.rawEnd && !/\s/.test(text.charAt(end))) {
      ++end;
    }
    return {
      start,
      end,
      cursor: segment.cursor,
      token: text.slice(start, end),
      cursorInsideCommand: segment.cursor <= end,
    };
  }

  function completeCommandNameWithTab() {
    const info = commandTokenInfo(input.value, input.selectionStart);
    if (!info || !info.cursorInsideCommand) {
      return false;
    }

    const typedToken = input.value.slice(info.start, info.cursor);
    if (!typedToken.startsWith('/')) {
      return false;
    }

    const selectedCommand = commandByInputToken(input.value, input.selectionStart);
    if (
      selectedCommand
      && info.cursor === info.end
      && typedToken.toLowerCase() === commandName(selectedCommand).toLowerCase()
    ) {
      applyCommandSuggestion(selectedCommand, true);
      return true;
    }

    const matches = commands.filter((command) =>
      commandName(command).toLowerCase().startsWith(typedToken.toLowerCase())
    );
    if (matches.length === 0) {
      renderCommandSuggestions();
      return true;
    }

    const names = matches.map(commandName).filter(Boolean);
    const shared = commonPrefix(names);
    if (matches.length === 1 || shared.length > typedToken.length) {
      const completed = matches.length === 1 ? commandName(matches[0]) : shared;
      const after = input.value.slice(info.end);
      const suffix = matches.length === 1 && (after.length === 0 || !/^\s/.test(after)) ? ' ' : '';
      const insertion = `${completed}${suffix}`;
      input.value = `${input.value.slice(0, info.start)}${insertion}${after}`;
      const cursor = info.start + insertion.length;
      input.setSelectionRange(cursor, cursor);
      syncComposerHeight();
      renderCommandSuggestions();
      return true;
    }

    renderCommandSuggestions();
    setActiveCommandSuggestion(0, true);
    return true;
  }

  function commandArguments(command) {
    return command && Array.isArray(command.arguments) ? command.arguments : [];
  }

  function containsUnquotedPipe(text) {
    let inQuotes = false;
    let escape = false;
    let quoteChar = '';
    for (let index = 0; index < text.length; ++index) {
      const ch = text.charAt(index);
      if (escape) {
        escape = false;
        continue;
      }
      if (ch === '\\') {
        escape = true;
        continue;
      }
      if (inQuotes) {
        if (ch === quoteChar) {
          inQuotes = false;
        }
        continue;
      }
      if (ch === '"' || ch === '\'') {
        inQuotes = true;
        quoteChar = ch;
        continue;
      }
      if (ch === '|') {
        return true;
      }
    }
    return false;
  }

  function argumentCompletionContext(args, cursor, argsMeta) {
    const result = {
      ok: false,
      cursor: Math.max(0, Math.min(Number(cursor) || 0, args.length)),
      argumentIndex: 0,
      replaceStart: Math.max(0, Math.min(Number(cursor) || 0, args.length)),
      replaceEnd: Math.max(0, Math.min(Number(cursor) || 0, args.length)),
      prefix: '',
      previousValues: [],
      tokenQuoted: false,
      startsNewToken: true,
      greedyArgument: false,
    };

    if (containsUnquotedPipe(args)) {
      return result;
    }

    const tokens = [];
    let current = '';
    let currentPrefix = '';
    let inToken = false;
    let inQuotes = false;
    let escape = false;
    let tokenWasQuoted = false;
    let tokenStart = 0;
    let quoteChar = '';

    const startToken = (index) => {
      inToken = true;
      tokenStart = index;
      current = '';
      currentPrefix = '';
      tokenWasQuoted = false;
      inQuotes = false;
      escape = false;
      quoteChar = '';
    };

    const appendTokenChar = (ch, index) => {
      current += ch;
      if (index < result.cursor) {
        currentPrefix += ch;
      }
    };

    const flushToken = (end) => {
      if (!inToken) {
        return;
      }
      tokens.push({
        value: current,
        prefix: currentPrefix,
        start: tokenStart,
        end,
        quoted: tokenWasQuoted,
      });
      inToken = false;
      inQuotes = false;
      escape = false;
      tokenWasQuoted = false;
      current = '';
      currentPrefix = '';
      quoteChar = '';
    };

    for (let index = 0; index < args.length; ++index) {
      const ch = args.charAt(index);
      if (!inToken) {
        if (/\s/.test(ch)) {
          continue;
        }
        startToken(index);
      }
      if (escape) {
        appendTokenChar(ch, index);
        escape = false;
        continue;
      }
      if (ch === '\\') {
        escape = true;
        continue;
      }
      if (inQuotes) {
        if (ch === quoteChar) {
          inQuotes = false;
          tokenWasQuoted = true;
          continue;
        }
        appendTokenChar(ch, index);
        continue;
      }
      if (ch === '"' || ch === '\'') {
        inQuotes = true;
        tokenWasQuoted = true;
        quoteChar = ch;
        continue;
      }
      if (/\s/.test(ch)) {
        flushToken(index);
        continue;
      }
      appendTokenChar(ch, index);
    }

    if (escape) {
      appendTokenChar('\\', Math.max(0, args.length - 1));
    }
    if (inToken) {
      flushToken(args.length);
    }

    let tokenIndex = 0;
    let activeTokenIndex = -1;
    tokens.some((token, index) => {
      const cursorAtTokenEnd =
        result.cursor === token.end
        && (result.cursor === args.length || !/\s/.test(args.charAt(result.cursor)));
      const cursorInsideToken = result.cursor >= token.start && result.cursor < token.end;
      if (cursorInsideToken || cursorAtTokenEnd) {
        activeTokenIndex = index;
        tokenIndex = index;
        return true;
      }
      if (token.end <= result.cursor) {
        tokenIndex = index + 1;
      }
      return false;
    });

    if (activeTokenIndex >= 0) {
      const active = tokens[activeTokenIndex];
      result.prefix = active.prefix;
      result.replaceStart = active.start;
      result.replaceEnd = active.end;
      result.tokenQuoted = active.quoted;
      result.startsNewToken = false;
    }

    if (argsMeta.length > 0) {
      result.argumentIndex = argsMeta.length;
      for (let index = 0; index < argsMeta.length; ++index) {
        const argument = argsMeta[index] || {};
        if (argument.greedy && tokenIndex >= index) {
          result.argumentIndex = index;
          result.greedyArgument = true;
          break;
        }
        if (tokenIndex === index) {
          result.argumentIndex = index;
          result.greedyArgument = Boolean(argument.greedy);
          break;
        }
      }
    }

    const previousTokenCount = Math.min(tokenIndex, tokens.length);
    for (let index = 0; index < previousTokenCount; ++index) {
      result.previousValues.push(tokens[index].value);
    }

    result.ok = true;
    return result;
  }

  function argumentCompletionCommandInfo(text, cursor) {
    const clampedCursor = Math.max(0, Math.min(Number(cursor) || 0, text.length));
    const segment = commandSegmentAtCursor(text, clampedCursor);
    if (!segment || clampedCursor < segment.start || text.charAt(segment.start) !== '/') {
      return null;
    }
    const commandStart = segment.start;

    let commandEnd = commandStart;
    while (commandEnd < segment.rawEnd && !/\s/.test(text.charAt(commandEnd))) {
      ++commandEnd;
    }
    if (clampedCursor <= commandEnd) {
      return null;
    }

    const command = commandMap().get(text.slice(commandStart, commandEnd).toLowerCase());
    if (!command) {
      return null;
    }

    let argsStart = commandEnd;
    while (argsStart < segment.rawEnd && /\s/.test(text.charAt(argsStart))) {
      ++argsStart;
    }

    return {
      command,
      argsStart,
      args: text.slice(argsStart, segment.rawEnd),
      cursor: clampedCursor,
    };
  }

  function completionToken(value) {
    const trimmed = String(value || '').trim();
    if (!trimmed) {
      return '';
    }
    if (!/[\s"\\]/.test(trimmed)) {
      return trimmed;
    }
    return `"${trimmed.replace(/(["\\])/g, '\\$1')}"`;
  }

  function completionConditionMatches(when, previousValues) {
    if (!when || typeof when !== 'object') {
      return true;
    }
    return Object.keys(when).every((key) => {
      const index = Number(key);
      if (!Number.isInteger(index)) {
        return true;
      }
      const expected = Array.isArray(when[key]) ? when[key] : [when[key]];
      const actual = String(previousValues[index] || '').trim().toLowerCase();
      return expected.some((value) => String(value || '').trim().toLowerCase() === actual);
    });
  }

  function localArgumentCompletion() {
    const text = input.value;
    const cursor = input.selectionStart == null ? text.length : input.selectionStart;
    const info = argumentCompletionCommandInfo(text, cursor);
    if (!info) {
      return null;
    }

    const argsMeta = commandArguments(info.command);
    const context = argumentCompletionContext(info.args, info.cursor - info.argsStart, argsMeta);
    if (!context.ok) {
      return null;
    }

    const entries = Array.isArray(info.command.argument_completions)
      ? info.command.argument_completions
      : [];
    const normalizedPrefix = context.prefix.trim();
    const seen = new Set();
    const candidates = [];
    entries.forEach((entry) => {
      if (!entry || Number(entry.argument) !== context.argumentIndex) {
        return;
      }
      if (!completionConditionMatches(entry.when, context.previousValues)) {
        return;
      }
      const values = Array.isArray(entry.values) ? entry.values : [];
      values.some((rawValue) => {
        if (candidates.length >= 50) {
          return true;
        }
        const raw = String(rawValue || '').trim();
        if (!raw) {
          return false;
        }
        if (
          normalizedPrefix
          && !raw.toLowerCase().startsWith(normalizedPrefix.toLowerCase())
        ) {
          return false;
        }
        const key = raw.toLowerCase();
        if (seen.has(key)) {
          return false;
        }
        seen.add(key);
        candidates.push({ value: completionToken(raw) });
        return false;
      });
    });

    return {
      replace_start: info.argsStart + context.replaceStart,
      replace_end: info.argsStart + context.replaceEnd,
      append_space: true,
      candidates,
    };
  }

  function applyArgumentCompletion(completion, candidate) {
    const value = String((candidate && candidate.value) || '');
    if (!value) {
      return;
    }

    const text = input.value;
    const replaceStart = Math.max(
      0,
      Math.min(Number(completion && completion.replace_start) || 0, text.length)
    );
    const replaceEnd = Math.max(
      replaceStart,
      Math.min(Number(completion && completion.replace_end) || replaceStart, text.length)
    );
    const after = text.slice(replaceEnd);
    const appendSpace = completion && completion.append_space !== false;
    const suffix = appendSpace && (after.length === 0 || !/^\s/.test(after)) ? ' ' : '';
    const insertion = `${value}${suffix}`;
    input.value = `${text.slice(0, replaceStart)}${insertion}${after}`;
    const cursor = replaceStart + insertion.length;
    input.focus();
    input.setSelectionRange(cursor, cursor);
    hideCommandSuggestions();
    syncComposerHeight();
    renderCommandSuggestions();
  }

  function renderArgumentSuggestions(completion, candidates) {
    commandSuggestions.innerHTML = '';
    commandSuggestions.dataset.mode = 'argument';
    activeCommandSuggestionIndex = -1;
    activeCommandSuggestionFromKeyboard = false;
    setCommandMenuButtonExpanded(false);

    candidates.forEach((candidate) => {
      const item = document.createElement('button');
      item.type = 'button';
      item.className = 'command-suggestion';

      const value = document.createElement('strong');
      value.textContent = String((candidate && candidate.value) || '');
      const description = document.createElement('span');
      description.textContent = String((candidate && candidate.description) || '');

      item.appendChild(value);
      item.appendChild(description);
      item.addEventListener('click', () => {
        applyArgumentCompletion(completion, candidate);
      });
      commandSuggestions.appendChild(item);
    });

    commandSuggestions.hidden = candidates.length === 0;
    if (candidates.length > 0) {
      setActiveCommandSuggestion(0);
    }
  }

  function requestArgumentCompletion() {
    const completion = localArgumentCompletion();
    if (!completion) {
      return false;
    }

    const candidates = Array.isArray(completion.candidates) ? completion.candidates : [];
    if (candidates.length === 0) {
      hideCommandSuggestions();
      return true;
    }
    if (candidates.length === 1) {
      applyArgumentCompletion(completion, candidates[0]);
      return true;
    }
    renderArgumentSuggestions(completion, candidates);
    return true;
  }

  function handleTabCompletion() {
    if (
      !commandSuggestions.hidden
      && commandSuggestions.dataset.mode === 'argument'
      && chooseVisibleCommandSuggestion()
    ) {
      return true;
    }

    if (completeCommandNameWithTab()) {
      return true;
    }

    const info = commandTokenInfo(input.value, input.selectionStart);
    if (!info || !commandByInputToken(input.value, input.selectionStart)) {
      return false;
    }

    return requestArgumentCompletion();
  }

  function renderCommandHelpSuggestion(command) {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'command-suggestion command-detail';

    const name = document.createElement('strong');
    name.textContent = commandUsage(command) || commandName(command);
    const description = document.createElement('span');
    description.textContent = commandHelpText(command);
    item.title = commandSuggestionTitle(command, description.textContent);

    item.appendChild(name);
    if (description.textContent) {
      item.appendChild(description);
    }
    item.addEventListener('click', () => {
      applyCommandSuggestion(command, true);
    });
    commandSuggestions.appendChild(item);
  }

  function appendCommandSuggestionItem(command, onClick) {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'command-suggestion';

    const name = document.createElement('strong');
    name.textContent = commandUsage(command) || commandName(command);
    const description = document.createElement('span');
    description.textContent = (command && command.description) || '';
    item.title = commandSuggestionTitle(command, description.textContent);

    item.appendChild(name);
    item.appendChild(description);
    item.addEventListener('click', onClick);
    commandSuggestions.appendChild(item);
  }

  function renderAllCommandSuggestions() {
    if (commands.length === 0) {
      hideCommandSuggestions();
      return;
    }

    commandSuggestions.innerHTML = '';
    commandSuggestions.dataset.mode = 'menu';
    activeCommandSuggestionIndex = -1;
    activeCommandSuggestionFromKeyboard = false;
    orderedCommands(commands).forEach((command) => {
      appendCommandSuggestionItem(command, () => {
        applyCommandSuggestion(command);
      });
    });
    commandSuggestions.hidden = false;
    setCommandMenuButtonExpanded(true);
    setActiveCommandSuggestion(0);
  }

  function renderCommandSuggestions() {
    const cursor = input.selectionStart == null ? input.value.length : input.selectionStart;
    const query = commandQuery(input.value, cursor);
    if (query === null) {
      hideCommandSuggestions();
      return;
    }

    const selectedCommand = commandByInputToken(input.value, cursor);
    if (selectedCommand) {
      commandSuggestions.innerHTML = '';
      commandSuggestions.dataset.mode = 'command';
      setCommandMenuButtonExpanded(false);
      renderCommandHelpSuggestion(selectedCommand);
      commandSuggestions.hidden = false;
      setActiveCommandSuggestion(0);
      return;
    }

    const matches = orderedCommands(
      commands.filter((command) => commandName(command).toLowerCase().startsWith(`/${query}`))
    )
      .slice(0, maxCommandSuggestionCount);

    if (matches.length === 0) {
      hideCommandSuggestions();
      return;
    }

    commandSuggestions.innerHTML = '';
    commandSuggestions.dataset.mode = 'command';
    activeCommandSuggestionIndex = -1;
    activeCommandSuggestionFromKeyboard = false;
    setCommandMenuButtonExpanded(false);
    matches.forEach((command) => {
      appendCommandSuggestionItem(command, () => {
        applyCommandSuggestion(command);
      });
    });
    commandSuggestions.hidden = false;
  }

  function toggleCommandMenu() {
    if (isSendingBlocked()) {
      return;
    }
    if (isCommandMenuOpen()) {
      hideCommandSuggestions();
      input.focus();
      return;
    }
    renderAllCommandSuggestions();
    if (!commandSuggestions.hidden) {
      input.focus();
    }
  }

  function createMessageElement(message, createdAt, isNewMessage) {
    const item = document.createElement('article');
    item.className = messageClass(message.type);
    if (isNewMessage) {
      item.classList.add('message-enter');
    }
    if (message.id) {
      item.dataset.historyId = String(message.id);
    }
    if (message.message_id) {
      item.dataset.messageId = String(message.message_id);
    }
    if (message.type === 'bot_file') {
      item.classList.add('with-file');
    }
    renderFileAttachment(item, message);
    const text = document.createElement('div');
    text.className = 'message-text';
    renderMessageText(text, message);
    item.appendChild(text);
    renderMessageFooter(item, message, createdAt);
    renderKeyboard(item, message);
    return item;
  }

  function findRenderedMessageElement(message) {
    const elements = Array.from(messageList.querySelectorAll('article'));
    const historyId = message && message.id ? String(message.id) : '';
    const messageId = message && message.message_id ? String(message.message_id) : '';
    return elements.find((element) =>
      (historyId && element.dataset.historyId === historyId)
      || (messageId && element.dataset.messageId === messageId)
    ) || null;
  }

  function replaceRenderedMessage(message) {
    const current = findRenderedMessageElement(message);
    if (!current) {
      return false;
    }
    const createdAt = messageDate(message);
    current.replaceWith(createMessageElement(message, createdAt, false));
    scheduleDayLabelRefresh();
    window.requestAnimationFrame(() => {
      messageList.scrollTop = messageList.scrollHeight;
      updateMessageListFadeState();
    });
    return true;
  }

  function renderMessages() {
    const history = window.BookingResourcesApp.loadHistory();
    const previousMessageIds = renderedMessageIds;
    const previousDayKeys = renderedDayKeys;
    const nextMessageIds = new Set();
    const nextDayKeys = new Set();
    let hasNewMessages = false;
    messageList.innerHTML = '';
    if (history.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'empty-state';
      empty.textContent = window.BookingResourcesI18n.text('no_messages');
      messageList.appendChild(empty);
      renderedMessageIds = nextMessageIds;
      renderedDayKeys = nextDayKeys;
      hasRenderedMessages = true;
      scheduleDayLabelRefresh();
      scheduleMessageListFadeStateUpdate();
      return;
    }

    let previousDay = '';
    let currentDayGroup = null;
    history.forEach((message) => {
      const createdAt = messageDate(message);
      const currentDay = dateKey(createdAt);
      if (currentDay !== previousDay) {
        const isNewDay = hasRenderedMessages && !previousDayKeys.has(currentDay);
        currentDayGroup = createDayGroup(createdAt, isNewDay);
        messageList.appendChild(currentDayGroup);
        nextDayKeys.add(currentDay);
        previousDay = currentDay;
      }

      const isNewMessage = Boolean(message.id)
        && hasRenderedMessages
        && !previousMessageIds.has(message.id);
      if (isNewMessage) {
        hasNewMessages = true;
      }
      if (message.id) {
        nextMessageIds.add(message.id);
      }
      currentDayGroup.appendChild(createMessageElement(message, createdAt, isNewMessage));
    });
    renderedMessageIds = nextMessageIds;
    renderedDayKeys = nextDayKeys;
    hasRenderedMessages = true;
    scheduleDayLabelRefresh();
    if (hasNewMessages) {
      messageList.scrollTo({ top: messageList.scrollHeight, behavior: 'smooth' });
      scheduleMessageListFadeStateUpdate();
      return;
    }
    messageList.scrollTop = messageList.scrollHeight;
    scheduleMessageListFadeStateUpdate();
  }

  function storeBotEvent(event) {
    if (event.type === 'user_message') {
      return null;
    }

    if (event.type === 'command_cache_invalidated') {
      window.BookingResourcesApp.clearCommandCache();
      commands = [];
      renderCommandSuggestions();
      loadCommands();
      return null;
    }

    if (event.type === 'bot_edit') {
      const history = window.BookingResourcesApp.loadHistory();
      const target = findEditableHistoryMessage(history, event);
      if (target) {
        if (Object.prototype.hasOwnProperty.call(event, 'text')) {
          target.text = String(event.text || '');
        }
        target.options = {
          ...(event.options || target.options || {}),
          message_id: event.message_id,
        };
        window.BookingResourcesApp.saveHistory(history);
      }
      clearPendingCallbackForEvent(event);
      return {
        type: 'bot_edit',
        render: !target || !replaceRenderedMessage(target),
      };
    }

    const message = {
      type: event.type || 'bot_message',
      message_id: event.message_id,
      text: event.text || '',
      options: { ...(event.options || {}), message_id: event.message_id },
      timestamp: event.timestamp,
      file_available: event.file_available,
      file_id: event.file_id || '',
      file_name: event.file_name || '',
      file_size: event.file_size || 0,
    };
    window.BookingResourcesApp.appendHistory(message);
    clearPendingCallbackForEvent(event);
    return {
      type: 'message',
      message,
      render: true,
    };
  }

  function schedulePollEvents(delayMs) {
    if (eventsPollInFlight) {
      return;
    }
    if (eventsPollTimerId !== null) {
      window.clearTimeout(eventsPollTimerId);
    }
    eventsPollTimerId = window.setTimeout(() => {
      eventsPollTimerId = null;
      pollEvents();
    }, Math.max(0, Number(delayMs) || 0));
  }

  async function pollEvents() {
    if (eventsPollInFlight) {
      return;
    }
    eventsPollInFlight = true;
    let nextDelayMs = pendingCallback ? eventsPendingDelayMs : eventsIdleDelayMs;
    try {
      const waitMs = pendingCallback ? eventsCallbackWaitMs : 0;
      const eventsPath = waitMs > 0
        ? `/api/events?wait=${encodeURIComponent(String(waitMs))}`
        : '/api/events';
      const payload = await window.BookingResourcesApp.request(
        eventsPath,
        { method: 'GET' }
      );
      const events = eventsFromPayload(payload);
      const changes = events.map(storeBotEvent).filter(Boolean);
      const incomingMessages = changes
        .filter((change) => change.type === 'message')
        .map((change) => change.message);
      if (changes.some((change) => change.render)) {
        renderMessages();
      }
      incomingMessages.forEach(notifyIncomingMessage);
      nextDelayMs = pendingCallback ? eventsPendingDelayMs : eventsIdleDelayMs;
      if (isServerAvailableFromPayload(payload)) {
        showError('');
      } else {
        showError(
          window.BookingResourcesI18n.text(
            'server_unavailable',
            'Server is not available.'
          ),
          true
        );
      }
    } catch (error) {
      nextDelayMs = eventsRetryDelayMs;
      if (error.status === 401) {
        window.BookingResourcesApp.redirectToAuthorize();
        return;
      }
      showError(error.message || window.BookingResourcesI18n.text('connection_lost'), true);
    } finally {
      eventsPollInFlight = false;
      schedulePollEvents(nextDelayMs);
    }
  }

  async function loadCommands() {
    const locale = currentCommandLocale();
    commandsLocale = locale;
    const cachedCommands = loadCommandCache(locale);
    if (cachedCommands.length > 0) {
      commands = cachedCommands;
      renderCommandSuggestions();
      renderMessages();
    }

    try {
      const loaded = await window.BookingResourcesApp.request(
        `/api/commands?locale=${encodeURIComponent(locale)}`,
        { method: 'GET' }
      );
      if (commandsLocale !== locale) {
        return;
      }
      commands = commandsSupportArgumentCache(loaded) ? loaded : [];
      if (commands.length > 0) {
        saveCommandCache(locale, commands);
      } else {
        window.BookingResourcesApp.clearCommandCache(locale);
      }
      renderCommandSuggestions();
      renderMessages();
    } catch (error) {
      if (commandsLocale === locale && cachedCommands.length === 0) {
        commands = [];
      }
    }
  }

  async function applyChatLocaleChange(locale, reason) {
    baseDocumentTitle = await window.BookingResourcesApp.applyPageTitle('chat_title', 'Chat');
    updateDocumentTitle();
    refreshDaySeparatorLabels();
    renderMessages();

    if (reason === 'load') {
      return;
    }

    commands = [];
    renderCommandSuggestions();
    loadCommands();
  }

  async function sendText(rawText) {
    if (isSendingBlocked()) {
      return;
    }

    const text = String(rawText || '').trim();
    if (!text) {
      return;
    }

    const localMessageId = `local-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    input.value = '';
    syncComposerHeight();
    hideCommandSuggestions();
    window.BookingResourcesApp.appendHistory({
      id: localMessageId,
      type: 'user_message',
      text,
      delivery_status: 'pending',
    });
    invalidateCommandUsageFrequencies();
    renderMessages();

    try {
      await window.BookingResourcesApp.request('/api/messages', {
        method: 'POST',
        body: JSON.stringify({ text }),
      });
      updateMessageStatus(localMessageId, 'delivered');
      showError('');
    } catch (error) {
      updateMessageStatus(localMessageId, 'failed');
      if (error.status === 401) {
        window.BookingResourcesApp.redirectToAuthorize();
        return;
      }
      showError(
        error.message || window.BookingResourcesI18n.text('connection_lost'),
        shouldHideComposerForRequestError(error)
      );
    }
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    await sendText(input.value);
  });

  messageList.addEventListener('scroll', updateMessageListFadeState, { passive: true });
  window.addEventListener('resize', () => {
    syncComposerHeight();
    scheduleMessageListFadeStateUpdate();
  });
  input.addEventListener('input', () => {
    syncComposerHeight();
    renderCommandSuggestions();
  });
  input.addEventListener('click', refreshComposerSuggestions);
  input.addEventListener('keyup', (event) => {
    if (event.key === 'Tab' || event.key === 'Enter' || event.key === 'Escape') {
      return;
    }
    if ((event.key === 'ArrowDown' || event.key === 'ArrowUp') && canNavigateCommandSuggestions()) {
      return;
    }
    refreshComposerSuggestions();
  });
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Tab') {
      if (!commandTokenInfo(input.value, input.selectionStart)) {
        event.preventDefault();
        const sendButton = form.querySelector('button[type="submit"]');
        if (sendButton) {
          sendButton.focus();
        }
        return;
      }
      event.preventDefault();
      handleTabCompletion();
      return;
    }

    if (event.key === 'ArrowDown') {
      if (moveActiveCommandSuggestion(1)) {
        event.preventDefault();
      }
      return;
    }

    if (event.key === 'ArrowUp') {
      if (moveActiveCommandSuggestion(-1)) {
        event.preventDefault();
      }
      return;
    }

    if (event.key === 'Enter') {
      if (event.isComposing || event.keyCode === 229) {
        return;
      }
      if (event.shiftKey) {
        return;
      }
      event.preventDefault();
      if (event.repeat) {
        return;
      }
      if (chooseActiveCommandSuggestion()) {
        return;
      }
      if (typeof form.requestSubmit === 'function') {
        form.requestSubmit();
      } else {
        form.dispatchEvent(new Event('submit', { cancelable: true, bubbles: true }));
      }
      return;
    }

    if (event.key === 'Escape' && !commandSuggestions.hidden) {
      event.preventDefault();
      hideCommandSuggestions();
    }
  });
  input.addEventListener('focus', () => {
    if (isCommandMenuOpen()) {
      return;
    }
    renderCommandSuggestions();
  });
  input.addEventListener('blur', () => {
    window.setTimeout(() => {
      if (keepCommandSuggestionsOnBlur) {
        keepCommandSuggestionsOnBlur = false;
        return;
      }
      if (document.activeElement === input) {
        return;
      }
      hideCommandSuggestions();
    }, 150);
  });
  commandSuggestions.addEventListener('mousedown', () => {
    keepCommandSuggestionsOnBlur = true;
  });
  if (openCommandMenuButton) {
    openCommandMenuButton.addEventListener('mousedown', () => {
      keepCommandSuggestionsOnBlur = true;
    });
    openCommandMenuButton.addEventListener('click', () => {
      toggleCommandMenu();
    });
  }
  if (openSettingsButton) {
    openSettingsButton.addEventListener('click', openSettingsPanel);
  }
  if (settingsCloseButton) {
    settingsCloseButton.addEventListener('click', closeSettingsPanel);
  }
  if (settingsCancelButton) {
    settingsCancelButton.addEventListener('click', closeSettingsPanel);
  }
  if (settingsPanel) {
    settingsPanel.addEventListener('click', (event) => {
      if (event.target === settingsPanel) {
        closeSettingsPanel();
      }
    });
  }
  document.addEventListener('mousedown', (event) => {
    if (!isCommandMenuOpen() || isCommandSuggestionsRelatedTarget(event.target)) {
      return;
    }
    hideCommandSuggestions();
  });
  document.addEventListener('focusin', (event) => {
    if (!isCommandMenuOpen() || isCommandSuggestionsRelatedTarget(event.target)) {
      return;
    }
    hideCommandSuggestions();
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && isCommandMenuOpen() && document.activeElement !== input) {
      event.preventDefault();
      hideCommandSuggestions();
      if (openCommandMenuButton) {
        openCommandMenuButton.focus();
      }
      return;
    }
    if (event.key === 'Escape' && isSettingsPanelOpen()) {
      event.preventDefault();
      closeSettingsPanel();
    }
  });
  window.addEventListener('focus', () => {
    clearIncomingNotifications();
    refreshDaySeparatorLabels();
    scheduleDayLabelRefresh();
  });
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      clearIncomingNotifications();
      refreshDaySeparatorLabels();
      scheduleDayLabelRefresh();
    }
  });

  if (window.BookingResourcesSettings && settingsPanel) {
    await window.BookingResourcesSettings.init({
      root: settingsPanel,
      session,
      onLocaleChanged: applyChatLocaleChange,
      onHistoryCleared: () => {
        invalidateCommandUsageFrequencies();
        renderMessages();
        renderCommandSuggestions();
      },
      onSaved: closeSettingsPanel,
    });
  }
  armNotificationPermissionPrompt();
  renderMessages();
  syncComposerHeight();
  loadCommands();
  pollEvents();
});
