export const guidelinesHtml = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Paimon Thumbnails - Guidelines</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #020203;
            --card-bg: rgba(255, 255, 255, 0.03);
            --card-border: rgba(255, 255, 255, 0.08);
            --primary: #8b5cf6;
            --primary-hover: #7c3aed;
            --text-main: #ffffff;
            --text-muted: #a1a1aa;
            --success: #22c55e;
            --error: #ef4444;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: 'Inter', sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 40px 20px;
            line-height: 1.6;
        }

        .container {
            max-width: 800px;
            width: 100%;
            z-index: 1;
        }

        h1 {
            font-size: 3rem;
            font-weight: 800;
            margin-bottom: 1rem;
            background: linear-gradient(135deg, #fff 0%, #a5b4fc 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            text-align: center;
        }

        .subtitle {
            text-align: center;
            color: var(--text-muted);
            margin-bottom: 3rem;
            font-size: 1.1rem;
        }

        .card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 16px;
            padding: 2rem;
            margin-bottom: 2rem;
            backdrop-filter: blur(10px);
        }

        h2 {
            font-size: 1.5rem;
            margin-bottom: 1.5rem;
            color: var(--primary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }

        .rule-list {
            list-style: none;
            display: flex;
            flex-direction: column;
            gap: 1rem;
        }

        .rule-item {
            display: flex;
            gap: 1rem;
            align-items: flex-start;
        }

        .icon {
            flex-shrink: 0;
            width: 24px;
            height: 24px;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 50%;
            font-weight: bold;
        }

        .icon.check {
            background: rgba(34, 197, 94, 0.1);
            color: var(--success);
        }

        .icon.cross {
            background: rgba(239, 68, 68, 0.1);
            color: var(--error);
        }

        .rule-content h3 {
            font-size: 1.1rem;
            margin-bottom: 0.25rem;
            font-weight: 600;
        }

        .rule-content p {
            color: var(--text-muted);
            font-size: 0.95rem;
        }

        .btn {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            padding: 12px 24px;
            background: var(--primary);
            color: white;
            text-decoration: none;
            border-radius: 12px;
            font-weight: 600;
            transition: all 0.2s;
            border: none;
            cursor: pointer;
            font-size: 1rem;
        }

        .btn:hover {
            background: var(--primary-hover);
            transform: translateY(-1px);
        }

        .btn-secondary {
            background: rgba(255, 255, 255, 0.1);
        }

        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.15);
        }

        .footer {
            margin-top: 4rem;
            text-align: center;
            color: var(--text-muted);
            font-size: 0.9rem;
        }

        /* Background Gradient */
        .bg-gradient {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: radial-gradient(circle at 50% 0%, rgba(139, 92, 246, 0.15), transparent 70%);
            z-index: -1;
            pointer-events: none;
        }

    </style>
</head>
<body>
    <div class="bg-gradient"></div>

    <div class="container">
        <h1>Thumbnail Guidelines</h1>
        <p class="subtitle">Follow these rules to ensure your thumbnails are approved quickly.</p>

        <div class="card">
            <h2>
                <span class="icon check">✓</span>
                What to Do
            </h2>
            <ul class="rule-list">
                <li class="rule-item">
                    <div class="icon check">✓</div>
                    <div class="rule-content">
                        <h3>Clean Gameplay</h3>
                        <p>Capture clear, unobstructed views of the level's gameplay or decoration.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon check">✓</div>
                    <div class="rule-content">
                        <h3>High Quality</h3>
                        <p>Ensure the image is crisp and not pixelated. Use the highest quality settings available.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon check">✓</div>
                    <div class="rule-content">
                        <h3>Representative</h3>
                        <p>Choose a frame that best represents the level's theme and style.</p>
                    </div>
                </li>
            </ul>
        </div>

        <div class="card">
            <h2>
                <span class="icon cross">✕</span>
                What to Avoid
            </h2>
            <ul class="rule-list">
                <li class="rule-item">
                    <div class="icon cross">✕</div>
                    <div class="rule-content">
                        <h3>No External UI</h3>
                        <p>Do not include any external overlays, FPS counters, cheat indicators, or menu buttons. The screenshot must be pure gameplay.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon cross">✕</div>
                    <div class="rule-content">
                        <h3>No Text Overlays</h3>
                        <p>Avoid adding text like "Verified by X" or level names unless they are part of the level's decoration.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon cross">✕</div>
                    <div class="rule-content">
                        <h3>No Edited Images</h3>
                        <p>Do not use Photoshop or other tools to heavily alter the colors or add effects that aren't in the level.</p>
                    </div>
                </li>
            </ul>
        </div>

        <div style="text-align: center; display: flex; gap: 1rem; justify-content: center;">
            <a href="/" class="btn btn-secondary">Back to Home</a>
        </div>

        <div class="footer">
            <p>&copy; 2025 Paimon Thumbnails. All rights reserved.</p>
        </div>
    </div>
</body>
</html>`;
