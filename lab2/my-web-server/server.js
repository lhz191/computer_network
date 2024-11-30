const express = require('express');
const path = require('path');
const WebSocket = require('ws');

const app = express();
const PORT = 8888;

// 设置静态文件目录
app.use(express.static('public'));

// 创建路由
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// 启动服务器
const server = app.listen(PORT, () => {
    console.log(`Server is running at http://localhost:${PORT}`);
});
