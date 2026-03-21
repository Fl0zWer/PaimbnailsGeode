/**
 * Reports controllers — reports, feedback, history, user reports
 */
import { corsHeaders } from '../middleware/cors.js';
import { verifyApiKey } from '../middleware/auth.js';
import { getR2Json, putR2Json, listR2Keys } from '../services/storage.js';
import { censorText } from '../services/profanity.js';

export async function handleSubmitReport(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, username, note } = body;

    if (!levelId) {
      return new Response(JSON.stringify({ error: 'Missing levelId' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const queueKey = `data/queue/reports/${levelId}.json`;
    const queueItem = {
      levelId: parseInt(levelId), category: 'report',
      submittedBy: username || 'unknown', timestamp: Date.now(),
      status: 'pending', note: censorText(note || 'No details provided')
    };
    await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);

    return new Response(JSON.stringify({ success: true, message: 'Report submitted successfully' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Submit report error:', error);
    return new Response(JSON.stringify({ error: 'Failed to submit report', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ── User Reports (report a user profile) ──

export async function handleSubmitUserReport(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { reportedAccountID, reportedUsername, note, reporterUsername, reporterAccountID } = body;

    if (!reportedAccountID || !reporterUsername) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Prevent self-reporting
    if (reporterAccountID && reporterAccountID === reportedAccountID) {
      return new Response(JSON.stringify({ error: 'Cannot report yourself' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Check reporter not banned
    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(reporterUsername.toLowerCase())) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const queueKey = `data/queue/reports/user_${reportedAccountID}.json`;
    let existing = await getR2Json(env.SYSTEM_BUCKET, queueKey);

    const reportEntry = {
      reporter: reporterUsername,
      reporterAccountID: parseInt(reporterAccountID) || 0,
      note: censorText((note || 'No details provided').substring(0, 200)),
      timestamp: Date.now()
    };

    if (existing && existing.reports) {
      // Check if this reporter already submitted
      const alreadyReported = existing.reports.some(
        r => r.reporter?.toLowerCase() === reporterUsername.toLowerCase()
      );
      if (alreadyReported) {
        return new Response(JSON.stringify({ error: 'You have already reported this user' }), {
          status: 409, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      existing.reports.push(reportEntry);
      existing.timestamp = Date.now();
      await putR2Json(env.SYSTEM_BUCKET, queueKey, existing);
    } else {
      const queueItem = {
        levelId: parseInt(reportedAccountID),
        category: 'report',
        type: 'user',
        reportedUsername: reportedUsername || 'unknown',
        submittedBy: reporterUsername,
        timestamp: Date.now(),
        status: 'pending',
        reports: [reportEntry]
      };
      await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }

    return new Response(JSON.stringify({ success: true, message: 'User report submitted' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Submit user report error:', error);
    return new Response(JSON.stringify({ error: 'Failed to submit user report', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleFeedbackSubmit(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { type, title, description, username } = body;

    if (!type || !title || !description) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const timestamp = Date.now();
    const id = `${timestamp}-${Math.random().toString(36).substring(2, 9)}`;
    const key = `data/feedback/${id}.json`;

    const feedbackData = {
      id, type, title: censorText(title), description: censorText(description),
      username: username || 'Anonymous', timestamp, status: 'pending',
      userAgent: request.headers.get('User-Agent'),
      ip: request.headers.get('CF-Connecting-IP')
    };

    await putR2Json(env.SYSTEM_BUCKET, key, feedbackData);

    return new Response(JSON.stringify({ success: true, message: 'Feedback submitted successfully', id }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Feedback submit error:', error);
    return new Response(JSON.stringify({ error: 'Failed to submit feedback', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleFeedbackList(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const keys = await listR2Keys(env.SYSTEM_BUCKET, 'data/feedback/');
    const dataResults = await Promise.all(keys.map(key => getR2Json(env.SYSTEM_BUCKET, key)));
    const items = dataResults.filter(Boolean);
    items.sort((a, b) => b.timestamp - a.timestamp);

    return new Response(JSON.stringify({ success: true, count: items.length, items }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Get feedback error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get feedback', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGetHistory(request, env, type) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const url = new URL(request.url);
    const limit = parseInt(url.searchParams.get('limit')) || 100;
    const levelId = url.searchParams.get('levelId');

    let prefix = `data/history/${type}/`;
    if (levelId) prefix += `${levelId}-`;

    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);
    const slice = keys.slice(0, limit);
    const dataResults = await Promise.all(slice.map(key => getR2Json(env.SYSTEM_BUCKET, key)));
    const items = dataResults.filter(Boolean);

    items.sort((a, b) => {
      const timeA = new Date(a.uploadedAt || a.acceptedAt || a.rejectedAt).getTime();
      const timeB = new Date(b.uploadedAt || b.acceptedAt || b.rejectedAt).getTime();
      return timeB - timeA;
    });

    return new Response(JSON.stringify({ success: true, type, count: items.length, items }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Get history error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get history', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
