export const donateHtml = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Paimon Thumbnails - Donate</title>
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
            overflow-x: hidden;
            position: relative;
        }

        /* Ambient Background */
        .bg-overlay {
            position: fixed;
            inset: 0;
            z-index: -1;
            background: radial-gradient(circle at 50% 0%, rgba(139, 92, 246, 0.15) 0%, rgba(2,2,3,0.95) 100%);
            pointer-events: none;
        }

        header {
            width: 100%;
            padding: 24px 40px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            z-index: 10;
            position: sticky;
            top: 0;
            backdrop-filter: blur(12px);
            border-bottom: 1px solid var(--card-border);
            background: rgba(2, 2, 3, 0.6);
        }

        .logo {
            text-decoration: none;
        }

        .logo h1 {
            margin: 0;
            font-size: 1.5rem;
            font-weight: 800;
            background: linear-gradient(to bottom right, #fff, #a1a1aa);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: -0.02em;
        }

        .container {
            max-width: 800px;
            width: 100%;
            padding: 60px 20px;
            z-index: 1;
            display: flex;
            flex-direction: column;
            align-items: center;
            flex-grow: 1;
            justify-content: center;
        }

        .donate-card {
            background: #09090b;
            border: 1px solid var(--card-border);
            border-radius: 20px;
            padding: 50px;
            text-align: center;
            width: 100%;
            box-shadow: 0 20px 40px rgba(0,0,0,0.5);
            position: relative;
            overflow: hidden;
            animation: fadeUp 0.8s ease-out;
        }
        
        .donate-card::before {
            content: '';
            position: absolute;
            top: 0; left: 0; right: 0;
            height: 4px;
            background: linear-gradient(90deg, #3b82f6, #8b5cf6, #ec4899);
        }

        .icon-container {
            width: 80px;
            height: 80px;
            background: rgba(2ec4899, 0.1);
            background: linear-gradient(135deg, rgba(139,92,246,0.2), rgba(236,72,153,0.2));
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            margin: 0 auto 24px;
            color: #ec4899;
            box-shadow: 0 0 30px rgba(236,72,153,0.2);
        }

        h2 {
            font-size: 2.5rem;
            margin-bottom: 16px;
            font-weight: 800;
            background: linear-gradient(to bottom right, #fff, #e2e8f0);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        p {
            font-size: 1.15rem;
            color: var(--text-muted);
            line-height: 1.6;
            margin-bottom: 30px;
        }

        .instruction-box {
            background: rgba(255,255,255,0.05);
            border: 1px dashed rgba(255,255,255,0.2);
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 40px;
        }

        .instruction-box p {
            margin: 0;
            font-size: 1rem;
            color: #e2e8f0;
        }

        .instruction-box strong {
            color: #facc15;
            font-weight: 700;
        }

        .donate-btn {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 12px;
            background: linear-gradient(135deg, #FF5E5B, #ec4899);
            color: white;
            text-decoration: none;
            padding: 18px 40px;
            border-radius: 30px;
            font-size: 1.25rem;
            font-weight: 700;
            transition: all 0.3s ease;
            box-shadow: 0 10px 25px rgba(255, 94, 91, 0.4);
        }

        .donate-btn:hover {
            transform: translateY(-3px);
            box-shadow: 0 15px 35px rgba(255, 94, 91, 0.6);
        }

        .back-link {
            margin-top: 30px;
            display: inline-block;
            color: var(--text-muted);
            text-decoration: none;
            font-size: 0.95rem;
            transition: color 0.2s;
        }

        .back-link:hover {
            color: white;
        }

        footer {
            margin-top: auto;
            padding: 40px;
            text-align: center;
            color: var(--text-muted);
            font-size: 0.9rem;
            width: 100%;
            border-top: 1px solid var(--card-border);
            background: rgba(2, 2, 3, 0.8);
            backdrop-filter: blur(12px);
        }

        @keyframes fadeUp {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }

        @media (max-width: 600px) {
            .donate-card { padding: 30px 20px; }
            h2 { font-size: 2rem; }
        }
    </style>
</head>
<body>
    <div class="bg-overlay"></div>

    <header>
        <a href="/" class="logo">
            <h1>Paimon Thumbnails</h1>
        </a>
    </header>

    <div class="container">
        <div class="donate-card">
            <div class="icon-container">
                <svg width="40" height="40" fill="currentColor" viewBox="0 0 24 24">
                    <path d="M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z"/>
                </svg>
            </div>
            
            <h2>Support the Project</h2>
            <p>Running the servers and adding new features takes time and resources. If you enjoy using Paimon Thumbnails, consider supporting us on Ko-fi to keep the project alive and thriving!</p>

            <div class="instruction-box">
                <p>⚠️ <strong>Important:</strong> Please include your <strong>Geometry Dash username</strong> in your Ko-fi donation message !</p>
            </div>

            <a href="https://ko-fi.com/flozwer" target="_blank" rel="noopener noreferrer" class="donate-btn">
                Donate on Ko-fi
                <svg width="24" height="24" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 6H6a2 2 0 00-2 2v10a2 2 0 002 2h10a2 2 0 002-2v-4M14 4h6m0 0v6m0-6L10 14"></path></svg>
            </a>

            <br>
            <a href="/" class="back-link">← Back to Home</a>
        </div>
    </div>

    <footer>
        <p>&copy; 2025 Paimon Thumbnails. Not affiliated with HoYoverse or RobTop Games.</p>
    </footer>
</body>
</html>`;
