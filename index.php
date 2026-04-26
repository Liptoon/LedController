<?php
// Konfiguracja
$upload_dir = __DIR__ . '/uploads/'; // Katalog docelowy
$target_file = $upload_dir . 'daily.png';
$target_motd_file = $upload_dir . 'motd.txt';
$message = '';
$message_type = '';
$motd_message = '';
$motd_message_type = '';

function is_allowed_image($tmp_path) {
    $allowed_mimes = ['image/jpeg', 'image/png', 'image/gif', 'image/bmp', 'image/x-ms-bmp'];
    if (!file_exists($tmp_path)) return false;
    $finfo = finfo_open(FILEINFO_MIME_TYPE);
    $mime_type = finfo_file($finfo, $tmp_path);
    finfo_close($finfo);
    return in_array($mime_type, $allowed_mimes);
}

// Przetwarzanie formularza obrazu
if ($_SERVER['REQUEST_METHOD'] === 'POST' && (isset($_FILES['image']) || isset($_POST['image_url']) || isset($_POST['image_base64']))) {
    if (!is_dir($upload_dir)) mkdir($upload_dir, 0755, true);
    $success = false;

    if (!empty($_FILES['image']['tmp_name'])) {
        if (is_allowed_image($_FILES['image']['tmp_name'])) {
            $success = move_uploaded_file($_FILES['image']['tmp_name'], $target_file);
        } else {
            $message = 'Błąd: Dozwolone tylko JPG, PNG, GIF, BMP.';
            $message_type = 'error';
        }
    } elseif (!empty($_POST['image_url'])) {
        $url_content = @file_get_contents($_POST['image_url'], false, stream_context_create(["http" => ["timeout" => 5, "user_agent" => "Mozilla/5.0"]]));
        if ($url_content) {
            $tmp_url_file = $upload_dir . 'tmp_url_check';
            file_put_contents($tmp_url_file, $url_content);
            if (is_allowed_image($tmp_url_file)) {
                rename($tmp_url_file, $target_file);
                $success = true;
            } else {
                unlink($tmp_url_file);
                $message = 'URL nie zawiera poprawnego obrazu (JPG/PNG/GIF/BMP).';
                $message_type = 'error';
            }
        } else {
            $message = 'Nie udało się pobrać obrazu z podanego adresu.';
            $message_type = 'error';
        }
    } elseif (!empty($_POST['image_base64'])) {
        $data = explode(',', $_POST['image_base64']);
        if (isset($data[1])) {
            $decoded_data = base64_decode($data[1]);
            $tmp_b64_file = $upload_dir . 'tmp_b64_check';
            file_put_contents($tmp_b64_file, $decoded_data);
            if (is_allowed_image($tmp_b64_file)) {
                rename($tmp_b64_file, $target_file);
                $success = true;
            } else {
                unlink($tmp_b64_file);
                $message = 'Dane ze schowka nie są poprawnym obrazem.';
                $message_type = 'error';
            }
        }
    }

    if ($success) {
        chmod($target_file, 0644);
        $message = 'Sukces! Plik został zapisany.';
        $message_type = 'success';
    }
}

// Przetwarzanie formularza MOTD
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['motd_text'])) {
    if (!is_dir($upload_dir)) {
        mkdir($upload_dir, 0755, true);
    }
    
    $motd_content = trim($_POST['motd_text']);
    if (file_put_contents($target_motd_file, $motd_content) !== false) {
        chmod($target_motd_file, 0644);
        $motd_message = 'Sukces! MOTD został zapisany.';
        $motd_message_type = 'success';
    } else {
        $motd_message = 'Błąd zapisu pliku motd.txt na serwerze.';
        $motd_message_type = 'error';
    }
}
?>
<!DOCTYPE html>
<html lang="pl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Daily LED</title>
    <style>
        :root {
            --bg-color: #121212;
            --surface-color: #1e1e1e;
            --primary-color: #4CAF50;
            --primary-hover: #45a049;
            --text-color: #e0e0e0;
            --error-color: #cf6679;
            --success-color: #03dac6;
        }
        body {
            font-family: 'Segoe UI', system-ui, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            padding: 2rem 1rem;
            box-sizing: border-box;
        }
        .wrapper {
            display: flex;
            flex-direction: column;
            gap: 2rem;
            width: 100%;
            max-width: 900px;
        }
        @media (min-width: 768px) {
            .wrapper {
                flex-direction: row;
            }
        }
        .container {
            background-color: var(--surface-color);
            padding: 2.5rem;
            border-radius: 12px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.5);
            text-align: center;
            flex: 1;
            box-sizing: border-box;
        }
        h1 { margin-top: 0; font-size: 1.5rem; font-weight: 500; }
        .file-input-wrapper { margin: 2rem 0; }
        input[type="file"] { display: none; }
        .custom-file-label {
            display: inline-block;
            padding: 12px 24px;
            cursor: pointer;
            border: 1px dashed rgba(255, 255, 255, 0.3);
            border-radius: 8px;
            transition: all 0.2s ease;
            color: #aaa;
            word-break: break-all;
            width: 100%;
            box-sizing: border-box;
        }
        .custom-file-label:hover {
            border-color: var(--primary-color);
            color: #fff;
            background-color: rgba(76, 175, 80, 0.05);
        }
        button {
            background-color: var(--primary-color);
            color: white;
            border: none;
            padding: 12px 24px;
            font-size: 1rem;
            border-radius: 6px;
            cursor: pointer;
            font-weight: 600;
            width: 100%;
            transition: background-color 0.2s;
        }
        button:hover { background-color: var(--primary-hover); }
        .message { margin-top: 1.5rem; padding: 12px; border-radius: 6px; font-size: 0.9rem; }
        .success { background-color: rgba(3, 218, 198, 0.1); color: var(--success-color); border: 1px solid var(--success-color); }
        .error { background-color: rgba(207, 102, 121, 0.1); color: var(--error-color); border: 1px solid var(--error-color); }
        
        .preview-box {
            margin-top: 1.5rem;
            padding-top: 1.5rem;
            border-top: 1px solid rgba(255, 255, 255, 0.1);
            font-size: 0.8rem;
            color: #888;
        }
        .preview-box img {
            display: block;
            margin: 8px auto 0;
            border-radius: 4px;
            image-rendering: pixelated;
            border: 1px solid rgba(255, 255, 255, 0.2);
        }
        
        textarea.motd-input {
            width: 100%;
            height: 120px;
            background-color: transparent;
            border: 1px dashed rgba(255, 255, 255, 0.3);
            border-radius: 8px;
            color: var(--text-color);
            padding: 12px;
            font-family: inherit;
            resize: vertical;
            box-sizing: border-box;
            transition: all 0.2s ease;
        }
        textarea.motd-input:focus {
            border-color: var(--primary-color);
            outline: none;
            background-color: rgba(76, 175, 80, 0.02);
        }
        .motd-preview-content {
            background: rgba(0,0,0,0.2);
            padding: 10px;
            border-radius: 6px;
            margin-top: 10px;
            word-wrap: break-word;
            white-space: pre-wrap;
            font-style: italic;
        }
    </style>
</head>
<body>
    <div class="wrapper">
        <div class="container">
            <h1>Wybierz grafikę na jutro</h1>
            <form action="" method="POST" enctype="multipart/form-data">
                <div class="file-input-wrapper">
                    <label for="file-upload" class="custom-file-label" id="file-label">
                        Wybierz obraz...
                    </label>
                    <input id="file-upload" type="file" name="image" accept="image/png, image/jpeg, image/gif, image/bmp">
                    <input type="hidden" name="image_base64" id="image-base64">
                    <input type="hidden" name="image_url" id="image-url">
                </div>
                <button type="submit">Zapisz jako daily.png</button>
            </form>

            <?php if ($message): ?>
                <div class="message <?= $message_type ?>"><?= $message ?></div>
            <?php endif; ?>

            <?php if (file_exists($target_file)): ?>
                <div class="preview-box">
                    obecna grafika na jutro:
                    <img src="uploads/daily.png?t=<?= filemtime($target_file) ?>" 
                         alt="Preview" width="64" height="64">
                </div>
            <?php endif; ?>
        </div>

        <div class="container">
            <h1>Ustaw MOTD</h1>
            <form action="" method="POST">
                <div class="file-input-wrapper">
                    <textarea name="motd_text" class="motd-input" placeholder="Wpisz tekst MOTD na jutro..." required></textarea>
                </div>
                <button type="submit">Zapisz tekst</button>
            </form>

            <?php if ($motd_message): ?>
                <div class="message <?= $motd_message_type ?>"><?= $motd_message ?></div>
            <?php endif; ?>

            <?php if (file_exists($target_motd_file)): ?>
                <div class="preview-box">
                    obecny MOTD na jutro:
                    <div class="motd-preview-content"><?= htmlspecialchars(file_get_contents($target_motd_file)) ?></div>
                </div>
            <?php endif; ?>
        </div>
    </div>

    <script>
        const dropZone = document.getElementById('file-label');
        const fileInput = document.getElementById('file-upload');
        const base64Input = document.getElementById('image-base64');
        const urlInput = document.getElementById('image-url');

        // Obsługa wklejania (Paste) - działa po naciśnięciu Ctrl+V gdziekolwiek na stronie
        window.addEventListener('paste', e => {
            const items = e.clipboardData.items;
            let handled = false;
            
            for (let item of items) {
                if (item.type.indexOf('image') !== -1) {
                    const blob = item.getAsFile();
                    const reader = new FileReader();
                    reader.onload = event => {
                        base64Input.value = event.target.result;
                        urlInput.value = '';
                        fileInput.value = ''; // Wyczyść fizyczny plik
                        dropZone.textContent = "Gotowe: Obraz ze schowka";
                        dropZone.style.borderColor = "var(--primary-color)";
                    };
                    reader.readAsDataURL(blob);
                    handled = true;
                    break;
                }
            }
            
            if (!handled) {
                const pastedText = e.clipboardData.getData('text');
                if (pastedText && (pastedText.startsWith('http://') || pastedText.startsWith('https://'))) {
                    urlInput.value = pastedText.trim();
                    base64Input.value = '';
                    fileInput.value = '';
                    dropZone.textContent = "Gotowe: Skopiowany adres URL";
                    dropZone.style.borderColor = "var(--primary-color)";
                }
            }
        });

        // Obsługa Drag & Drop bezpośrednio na istniejącej etykiecie
        dropZone.addEventListener('dragover', e => { 
            e.preventDefault(); 
            dropZone.style.borderColor = "var(--primary-color)"; 
            dropZone.style.backgroundColor = "rgba(76, 175, 80, 0.05)";
        });
        
        dropZone.addEventListener('dragleave', e => { 
            dropZone.style.borderColor = "rgba(255, 255, 255, 0.3)"; 
            dropZone.style.backgroundColor = "transparent";
        });
        
        dropZone.addEventListener('drop', e => {
            e.preventDefault();
            const files = e.dataTransfer.files;
            if (files.length > 0) {
                fileInput.files = files;
                base64Input.value = '';
                urlInput.value = '';
                dropZone.textContent = files[0].name;
            }
        });

        // Tradycyjny wybór pliku dyskowego
        fileInput.addEventListener('change', () => {
            if(fileInput.files.length > 0) {
                base64Input.value = '';
                urlInput.value = '';
                dropZone.textContent = fileInput.files[0].name;
                dropZone.style.borderColor = "var(--primary-color)";
            }
        });
    </script>
</body>
</html>