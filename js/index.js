const express = require('express');
const fs = require('fs');
const path = require('path');
const bodyParser = require('body-parser');
const cookieParser = require('cookie-parser');

const app = express();
const PORT = 8080;

app.use(express.text());
app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));
app.use(cookieParser());

// Handler for /echo
app.post('/echo', (req, res) => {
    res.setHeader('Content-Type', 'text/plain');
    res.send(req.body);
});

// Handler for /timestamp
app.get('/timestamp', (req, res) => {
    res.setHeader('Content-Type', 'text/plain');
    const now = new Date();
    res.send(now.toUTCString());
});

// Handler for /status
app.get('/status', (req, res) => {
    res.setHeader('Content-Type', 'text/plain');
    res.send('Server is running smoothly!');
});

app.get('/headers', (req, res) => {
    const headers = req.headers;
    res.setHeader('Content-Type', 'application/json');
    res.json({ headers: headers });
});

app.get('/redirect-to', (req, res) => {
    const targetUrl = req.query.url;
    if (targetUrl === '/get') {
        const headers = req.headers;
        res.setHeader('Content-Type', 'application/json');
        res.json({ headers: headers });
    } else {
        res.redirect(targetUrl);
    }
});

app.get('/redirect/:count', (req, res) => {
    const redirectCount = parseInt(req.params.count, 10);
    if (redirectCount > 0) {
        const nextRedirect = `/redirect/${redirectCount - 1}`;
        res.redirect(nextRedirect);
    } else {
        res.setHeader('Content-Type', 'application/json');
        res.json({ message: "Final destination reached!" });
    }
});

app.get('/image', (req, res) => {
    const imagePath = path.join(__dirname, 'requests-test.png');

    if (!fs.existsSync(imagePath)) {
        res.status(404).send('File not found');
    } else {
        res.setHeader('Content-Type', 'image/png');
        res.sendFile(imagePath);
    }
});

app.get('/redirect-to', (req, res) => {
    if (req.query.url === '/image') {
        res.redirect('/image');
    }
});

app.delete('/delete', (req, res) => {
    const body = req.body;

    if (body["test-key"] && body["test-key"] === "test-value") {
        res.json({ status: "success" });
    } else {
        res.status(400).json({ status: "failure" });
    }
});

app.patch('/patch', (req, res) => {
    if (req.is('application/x-www-form-urlencoded')) {
        const form = req.body;
        res.json({ status: "success", form: form });
    } else {
        const body = req.body;
        if (body["test-key"] === "test-value") {
            res.json({ status: "success" });
        } else {
            res.status(400).json({ status: "failure" });
        }
    }
});

app.put('/put', (req, res) => {
    if (req.is('application/x-www-form-urlencoded')) {
        const formData = req.body;

        if (formData["foo"] === "42" && formData["bar"] === "21" && formData["foo bar"] === "23") {
            res.send('foo=42&bar=21&foo%20bar=23');
        } else {
            res.status(400).send('Invalid form data');
        }
    } else if (req.is('application/json')) {
        const body = req.body;
        if (body["test-key"] === "test-value") {
            res.json({ status: "success" });
        } else {
            res.status(400).send('Invalid JSON data');
        }
    }
});

app.post('/post', (req, res) => {
    if (req.is('application/x-www-form-urlencoded')) {
        const formData = req.body;

        if (formData["foo"] === "42" && formData["bar"] === "21" && formData["foo bar"] === "23") {
            res.send('foo=42&bar=21&foo%20bar=23');
        } else {
            res.status(400).send('Invalid form data');
        }
    } else if (req.is('application/json')) {
        const body = req.body;
        if (body["test-key"] === "test-value") {
            res.json({ message: "Data received" });
        } else {
            res.status(400).json({ error: "Invalid JSON data" });
        }
    }
});

app.post('/post', (req, res) => {

});

app.get('/get', (req, res) => {
    res.json({ headers: req.headers });
});

app.get('/cookies/set', (req, res) => {
    for (let key in req.query) {
        res.cookie(key, req.query[key]);
    }

    res.json({ cookies: { "cookie-1": "foo", "cookie-2": "bar" } });
});

app.get('/cookies/delete', (req, res) => {
    for (let key in req.query) {
        res.clearCookie(key);
    }

    res.json({ deleted: req.query });
});

app.get('/cookies', (req, res) => {
    res.json({ cookies: {} });
});


app.listen(PORT, () => {
    console.log(`Server running on http://localhost:${PORT}`);
});
