// =====================================================================
//  Hokoro Cloudflare Worker
//
//  Cron (5分間隔):
//    - refresh_token を使って Graph API の access_token を更新
//    - /me/calendarView で今後7日の予定を取得
//    - start/end のみに整形して KV に保存 (タイトル等は破棄)
//
//  HTTP GET /schedule.json:
//    - KV にキャッシュされた予定 JSON を返す (ESP32 が取得する)
//
//  HTTP POST /refresh:
//    - 手動トリガ (テスト/デバッグ用)
//    - X-Refresh-Key ヘッダで認証
// =====================================================================

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);

    // 予定 JSON の配信
    if (url.pathname === '/' || url.pathname === '/schedule.json') {
      const data = await env.SCHEDULE.get('events');
      if (!data) {
        return jsonResponse({ error: 'no_data_yet' }, 503);
      }
      return new Response(data, {
        headers: {
          'Content-Type': 'application/json; charset=utf-8',
          'Cache-Control': 'public, max-age=60',
        },
      });
    }

    // 手動リフレッシュ (デバッグ用、X-Refresh-Key ヘッダ要)
    if (url.pathname === '/refresh' && request.method === 'POST') {
      const auth = request.headers.get('X-Refresh-Key');
      if (!env.REFRESH_KEY || auth !== env.REFRESH_KEY) {
        return new Response('Unauthorized', { status: 401 });
      }
      try {
        const result = await updateSchedule(env);
        return jsonResponse({ ok: true, ...result });
      } catch (e) {
        return jsonResponse({ ok: false, error: String(e) }, 500);
      }
    }

    return new Response('Not Found', { status: 404 });
  },

  async scheduled(event, env, ctx) {
    ctx.waitUntil(
      updateSchedule(env).catch((err) => {
        console.error('updateSchedule failed:', err);
      })
    );
  },
};

// ---- ユーティリティ ----
function jsonResponse(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  });
}

async function getRefreshToken(env) {
  // KV に新しい refresh_token がローテーション保存されていれば優先、
  // なければ初回投入分の env.REFRESH_TOKEN を使用
  const kvToken = await env.SCHEDULE.get('refresh_token');
  return kvToken || env.REFRESH_TOKEN;
}

async function updateSchedule(env) {
  // 1) refresh_token から access_token を取得
  const refreshToken = await getRefreshToken(env);
  const tokenResp = await fetch(
    `https://login.microsoftonline.com/${env.TENANT_ID}/oauth2/v2.0/token`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        client_id: env.CLIENT_ID,
        client_secret: env.CLIENT_SECRET,
        refresh_token: refreshToken,
        grant_type: 'refresh_token',
        scope: 'Calendars.Read offline_access User.Read',
      }).toString(),
    }
  );
  if (!tokenResp.ok) {
    const errBody = await tokenResp.text();
    throw new Error(`token refresh ${tokenResp.status}: ${errBody}`);
  }
  const tokens = await tokenResp.json();

  // refresh_token がローテートされていれば KV に保存
  if (tokens.refresh_token && tokens.refresh_token !== refreshToken) {
    await env.SCHEDULE.put('refresh_token', tokens.refresh_token);
    console.log('refresh_token rotated and saved to KV');
  }

  // 2) Graph API でカレンダー取得 (今 - 5分 〜 +7日)
  const now = new Date();
  const start = new Date(now.getTime() - 5 * 60 * 1000);
  const end = new Date(now.getTime() + 7 * 24 * 60 * 60 * 1000);

  const qs = new URLSearchParams({
    startDateTime: start.toISOString(),
    endDateTime: end.toISOString(),
    '$select': 'start,end,isAllDay,isCancelled,showAs',
    '$orderby': 'start/dateTime',
    '$top': '50',
  });
  const calResp = await fetch(
    `https://graph.microsoft.com/v1.0/me/calendarView?${qs.toString()}`,
    {
      headers: {
        Authorization: `Bearer ${tokens.access_token}`,
        // dateTime を UTC で返させる
        Prefer: 'outlook.timezone="UTC"',
      },
    }
  );
  if (!calResp.ok) {
    const errBody = await calResp.text();
    throw new Error(`graph api ${calResp.status}: ${errBody}`);
  }
  const cal = await calResp.json();

  // 3) フィルタ & 整形 (title/location/attendees 等は破棄)
  const events = (cal.value || [])
    .filter((e) => !e.isCancelled && !e.isAllDay && e.showAs !== 'free')
    .map((e) => ({
      start: toEpochSec(e.start.dateTime),
      end: toEpochSec(e.end.dateTime),
    }))
    .filter((e) => e.start && e.end);

  // 4) KV へ保存
  const payload = JSON.stringify({
    updated_at: new Date().toISOString(),
    count: events.length,
    events,
  });
  await env.SCHEDULE.put('events', payload);
  console.log(`updateSchedule: stored ${events.length} events`);

  return { count: events.length };
}

// Graph API が返す dateTime ("2026-06-18T07:00:00.0000000") を UTC epoch (秒) に変換
function toEpochSec(s) {
  if (!s) return 0;
  // 末尾が Z でも +HH:MM でもない場合は UTC として 'Z' を補う
  let iso = s.replace(/\.\d+$/, ''); // 微小秒を削る
  if (!/[Zz]$/.test(iso) && !/[+-]\d\d:?\d\d$/.test(iso)) {
    iso += 'Z';
  }
  const t = Date.parse(iso);
  if (isNaN(t)) return 0;
  return Math.floor(t / 1000);
}
