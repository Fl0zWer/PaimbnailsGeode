export const homeHtml = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Paimon Thumbnails - Home</title>
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

        /* Carousel Background */
        #background-carousel {
            position: fixed;
            inset: 0;
            z-index: -2;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            justify-content: center;
            gap: 24px;
            opacity: 0.6;
            pointer-events: none;
            transform: skewY(-5deg) scale(1.2);
            filter: blur(8px);
        }

        .carousel-row {
            display: flex;
            gap: 24px;
            width: max-content;
            will-change: transform;
        }

        .carousel-item {
            width: 240px;
            height: 135px;
            border-radius: 12px;
            background-size: cover;
            background-position: center;
            background-color: rgba(255,255,255,0.05);
            box-shadow: 0 4px 20px rgba(0,0,0,0.3);
            flex-shrink: 0;
        }

        .scroll-left { animation: scrollLeft 60s linear infinite; }
        .scroll-right { animation: scrollRight 60s linear infinite; }

        @keyframes scrollLeft { 0% { transform: translateX(0); } 100% { transform: translateX(-50%); } }
        @keyframes scrollRight { 0% { transform: translateX(-50%); } 100% { transform: translateX(0); } }

        /* Overlay */
        .bg-overlay {
            position: fixed;
            inset: 0;
            z-index: -1;
            background: radial-gradient(circle at center, rgba(2,2,3,0.4) 0%, rgba(2,2,3,0.95) 100%);
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
            max-width: 1200px;
            width: 100%;
            padding: 60px 20px;
            z-index: 1;
            display: flex;
            flex-direction: column;
            align-items: center;
            flex-grow: 1;
            justify-content: center;
        }

        .intro {
            text-align: center;
            margin-bottom: 60px;
            max-width: 700px;
            animation: fadeUp 0.8s ease-out;
        }
        
        .intro h2 {
            font-size: 3.5rem;
            margin-bottom: 20px;
            line-height: 1.1;
            background: linear-gradient(to bottom right, #fff, #a1a1aa);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            font-weight: 800;
            letter-spacing: -0.03em;
        }

        .intro p {
            font-size: 1.25rem;
            color: var(--text-muted);
            line-height: 1.6;
        }

        .nav-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 24px;
            width: 100%;
            max-width: 1000px;
            animation: fadeUp 0.8s ease-out 0.2s backwards;
        }

        .nav-card {
            background: #09090b;
            border: 1px solid #27272a;
            border-radius: 16px;
            padding: 40px;
            text-align: left;
            transition: all 0.3s ease;
            display: flex;
            flex-direction: column;
            text-decoration: none;
            color: inherit;
            position: relative;
            overflow: hidden;
        }

        .nav-card:hover {
            transform: translateY(-4px);
            box-shadow: 0 20px 40px -10px rgba(0,0,0,0.5);
            border-color: var(--primary);
            background: #0c0a14;
        }

        .nav-icon {
            width: 48px;
            height: 48px;
            background: rgba(139, 92, 246, 0.1);
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 24px;
            color: var(--primary);
        }

        .nav-title {
            font-size: 1.5rem;
            font-weight: 700;
            color: #fff;
            margin-bottom: 12px;
        }

        .nav-desc {
            font-size: 1rem;
            color: var(--text-muted);
            line-height: 1.5;
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

        @media (max-width: 768px) {
            .intro h2 { font-size: 2.5rem; }
            .nav-grid { grid-template-columns: 1fr; }
        }

        /* Special Thanks Section */
        .special-thanks {
            width: 100%;
            max-width: 1000px;
            margin-top: 80px;
            animation: fadeUp 0.8s ease-out 0.4s backwards;
        }

        .section-title {
            font-size: 2rem;
            font-weight: 800;
            margin-bottom: 32px;
            text-align: center;
            background: linear-gradient(to right, #fff, #a1a1aa);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .moderators-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(400px, 1fr));
            gap: 24px;
        }

        .mod-card {
            background: #0f172a;
            border-radius: 12px;
            overflow: hidden;
            position: relative;
            height: 140px;
            display: flex;
            box-shadow: 0 10px 30px -10px rgba(0,0,0,0.5);
            border: 1px solid rgba(255,255,255,0.1);
            transition: transform 0.3s ease;
        }

        .mod-card:hover {
            transform: translateY(-4px);
        }

        .mod-bg-left {
            position: absolute;
            left: 0; top: 0; bottom: 0;
            width: 40%;
            background: linear-gradient(135deg, #a855f7, #7c3aed);
            clip-path: polygon(0 0, 100% 0, 85% 100%, 0% 100%);
            z-index: 1;
        }

        .mod-bg-right {
            position: absolute;
            right: 0; top: 0; bottom: 0;
            width: 70%;
            background: linear-gradient(to right, #3b82f6, #0ea5e9);
            background-image: radial-gradient(circle at 80% 50%, rgba(255,255,255,0.1) 0%, transparent 50%);
            z-index: 0;
        }


        .mod-content {
            position: relative;
            z-index: 2;
            display: flex;
            width: 100%;
            align-items: center;
            padding: 0 24px;
        }

        .mod-icon-container {
            width: 80px;
            display: flex;
            justify-content: center;
            margin-right: 20px;
        }

        .mod-icon {
            width: 64px;
            height: 64px;
            filter: drop-shadow(0 4px 8px rgba(0,0,0,0.3));
        }

        .mod-info {
            flex: 1;
            display: flex;
            flex-direction: column;
            justify-content: center;
        }

        .mod-name {
            font-family: 'Inter', sans-serif;
            font-size: 1.5rem;
            font-weight: 900;
            color: #facc15;
            text-shadow: 2px 2px 0px #000;
            margin-bottom: 8px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        .mod-stats {
            display: flex;
            gap: 12px;
            flex-wrap: wrap;
        }

        .mod-stat {
            display: flex;
            align-items: center;
            gap: 4px;
            color: #fff;
            font-size: 0.85rem;
            font-weight: 700;
            text-shadow: 1px 1px 2px rgba(0,0,0,0.8);
        }

        .stat-icon {
            width: 16px;
            height: 16px;
            filter: drop-shadow(1px 1px 1px rgba(0,0,0,0.5));
        }
    </style>
</head>
<body>
    <!-- Background Elements -->
    <div id="background-carousel"></div>
    <div class="bg-overlay"></div>

    <header>
        <div class="logo">
            <h1>Paimon Thumbnails</h1>
        </div>
    </header>

    <div class="container">
        <div class="intro">
            <h2>Welcome to Paimon Thumbnails</h2>
            <p>The ultimate thumbnail manager for Geometry Dash. Customize your experience, support the mod, and join the community.</p>
        </div>

        <div class="nav-grid">
            <a href="https://github.com/Fl0zWer/Paimbnails/releases/tag/v1.0.1" class="nav-card" target="_blank" rel="noopener noreferrer">
                <div class="nav-icon">
                    <svg width="24" height="24" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 16v1a3 3 0 003 3h10a3 3 0 003-3v-1m-4-4l-4 4m0 0l-4-4m4 4V4"></path></svg>
                </div>
                <div class="nav-title">Download Mod</div>
                <div class="nav-desc">Get the latest version of Paimon Thumbnails for Geometry Dash.</div>
            </a>

            <a href="/guidelines" class="nav-card">
                <div class="nav-icon">
                    <svg width="24" height="24" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 6.253v13m0-13C10.832 5.477 9.246 5 7.5 5S4.168 5.477 3 6.253v13C4.168 18.477 5.754 18 7.5 18s3.332.477 4.5 1.253m0-13C13.168 5.477 14.754 5 16.5 5c1.747 0 3.332.477 4.5 1.253v13C19.832 18.477 18.247 18 16.5 18c-1.746 0-3.332.477-4.5 1.253"></path></svg>
                </div>
                <div class="nav-title">Guidelines</div>
                <div class="nav-desc">Read the rules and guidelines for uploading thumbnails to the gallery.</div>
            </a>
        </div>

        <div class="special-thanks">
            <h2 class="section-title">Special Thanks</h2>
            <div class="moderators-grid" id="moderatorsGrid">
                <!-- Moderators will be loaded here -->
                <div style="text-align: center; width: 100%; color: var(--text-muted);">Loading moderators...</div>
            </div>
        </div>
    </div>

    <footer>
        <p>&copy; 2025 Paimon Thumbnails. Not affiliated with HoYoverse or RobTop Games.</p>
    </footer>

    <script>
        // --- Carousel System ---
        async function initCarousel() {
            const container = document.getElementById('background-carousel');
            const rowCount = 5;
            
            const staticThumbnails = [
                "107110380", "100745189", "128124214", "127540093", "128082328", "112915492", "119871101", "99041912", 
                "98972163", "98439428", "98275850", "98128088", "98090232", "98081390", "97978413", "97666660", 
                "97459884", "96925293", "84202969", "83786418", "83652181", "80809056", "6508283", "4454123", 
                "31280642", "27732941", "26618473", "215705", "13519", "127956128", "127917376", "127896329", 
                "127857794", "127823828", "127793312", "127612214", "127611040", "127602122", "127283931", "126961267", 
                "126539075", "126414641", "126251887", "125092981", "123528431", "122833143", "120679939", "120093969", 
                "119749893", "119601181", "11940", "119259720", "118704447", "118467875", "117692206", "117630745", 
                "117043292", "116284799", "116270632", "116217763", "115050645", "115050301", "114032680", "113774286", 
                "113511091", "113473543", "113122387", "11280109", "112755295", "112158428", "111761303", "110610038", 
                "110022482", "109020681", "108580202", "108580172", "105797505", "105797326", "105189715", "104903779", 
                "104903768", "103043175", "102010564", "101601210", "100475191", "127436719", "86177226", "89586053", 
                "105711757", "107506579", "107902484", "109880290", "110019075", "110225752", "110998214", "115018298", 
                "115710733", "120235465", "127746356"
            ];

            try {
                let thumbnails = staticThumbnails.map(id => ({ levelId: id }));
                
                for (let i = 0; i < rowCount; i++) {
                    const row = document.createElement('div');
                    row.classList.add('carousel-row');
                    
                    if (i % 2 === 0) row.classList.add('scroll-right');
                    else row.classList.add('scroll-left');
                    
                    const duration = 60 + Math.random() * 40;
                    row.style.animationDuration = \`\${duration}s\`;

                    const rowItems = [...thumbnails].sort(() => 0.5 - Math.random()).slice(0, 20);
                    
                    const createItems = () => {
                        rowItems.forEach(item => {
                            const div = document.createElement('div');
                            div.classList.add('carousel-item');
                            div.style.backgroundImage = \`url('/t/\${item.levelId}.webp')\`;
                            row.appendChild(div);
                        });
                    };

                    createItems();
                    createItems();
                    createItems();

                    container.appendChild(row);
                }

            } catch (error) {
                console.error('Failed to init carousel:', error);
            }
        }

        initCarousel();

        // --- Moderators System ---
        async function loadModerators() {
            const grid = document.getElementById('moderatorsGrid');
            try {
                const res = await fetch('/api/moderators');
                if (!res.ok) throw new Error('Failed to load moderators');
                const data = await res.json();
                const moderators = data.moderators || [];

                if (moderators.length === 0) {
                    grid.innerHTML = '<div style="text-align: center; width: 100%; color: var(--text-muted);">No moderators found.</div>';
                    return;
                }

                grid.innerHTML = ''; // Clear loading

                // Fetch data for each moderator
                for (const modData of moderators) {
                    // Handle both string (legacy) and object format
                    const username = typeof modData === 'string' ? modData : modData.username;

                    try {
                        const gdRes = await fetch(\`https://gdbrowser.com/api/profile/\${username}\`);
                        if (!gdRes.ok) continue;
                        const user = await gdRes.json();
                        
                        const card = document.createElement('div');
                        card.className = 'mod-card';
                        
                        // Helper to convert RGB to Hex
                        const rgbToHex = (rgb) => {
                            if (!rgb) return null;
                            const toHex = (c) => {
                                const hex = c.toString(16);
                                return hex.length === 1 ? '0' + hex : hex;
                            };
                            return toHex(rgb.r) + toHex(rgb.g) + toHex(rgb.b);
                        };

                        // Construct Icon URL with explicit parameters to avoid lookup failures
                        // Use RGB if available for accurate colors
                        let col1 = rgbToHex(user.col1RGB) || user.col1;
                        let col2 = rgbToHex(user.col2RGB) || user.col2;
                        let iconUrl = \`https://gdbrowser.com/icon/\${user.username}?icon=\${user.icon}&col1=\${col1}&col2=\${col2}&glow=\${user.glow ? 1 : 0}\`;

                        let backgroundHtml = \`
                                <div class="mod-bg-left"></div>
                                <div class="mod-bg-right"></div>
                            \`;

                        card.innerHTML = \`
                            \${backgroundHtml}
                            <div class="mod-content">
                                <div class="mod-icon-container">
                                    <img src="\${iconUrl}" alt="\${user.username}" class="mod-icon">
                                </div>
                                <div class="mod-info">
                                    <div class="mod-name">\${user.username}</div>
                                    <div class="mod-stats">
                                        <div class="mod-stat" title="Stars">
                                            <img src="https://gdbrowser.com/assets/star.png" class="stat-icon">
                                            \${user.stars}
                                        </div>
                                        <div class="mod-stat" title="Moons">
                                            <img src="https://gdbrowser.com/assets/moon.png" class="stat-icon">
                                            \${user.moons}
                                        </div>
                                        <div class="mod-stat" title="Diamonds">
                                            <img src="https://gdbrowser.com/assets/diamond.png" class="stat-icon">
                                            \${user.diamonds}
                                        </div>
                                        <div class="mod-stat" title="User Coins">
                                            <img src="https://gdbrowser.com/assets/silvercoin.png" class="stat-icon">
                                            \${user.userCoins}
                                        </div>
                                        <div class="mod-stat" title="Demons">
                                            <img src="https://gdbrowser.com/assets/demon.png" class="stat-icon">
                                            \${user.demons}
                                        </div>
                                        <div class="mod-stat" title="Creator Points">
                                            <img src="https://gdbrowser.com/assets/cp.png" class="stat-icon">
                                            \${user.cp}
                                        </div>
                                    </div>
                                </div>
                            </div>
                        \`;
                        grid.appendChild(card);
                    } catch (e) {
                        console.error(\`Failed to load data for \${username}\`, e);
                    }
                }

            } catch (error) {
                console.error(error);
                grid.innerHTML = '<div style="text-align: center; width: 100%; color: var(--text-muted);">Failed to load moderators.</div>';
            }
        }

        loadModerators();
    </script>
</body>
</html>`;
