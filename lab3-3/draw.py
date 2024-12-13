import matplotlib.pyplot as plt
import numpy as np

# 数据
loss_rates = [1, 5, 10]
delays = [1, 10, 20]

# 传输时间数据
normal_loss = [3.6864, 4.1254, 4.0654]  # 丢包率 1%, 5%, 10% (GBN)
congestion_loss = [3.2029, 3.3256, 3.9275]  # 丢包率 1%, 5%, 10% (拥塞控制)

normal_delay = [19.8958, 25.2694, 42.3379]  # 延时 1ms, 10ms, 20ms (GBN)
congestion_delay = [14.0419, 16.8140, 26.1960]  # 延时 1ms, 10ms, 20ms (拥塞控制)

# 图1: 丢包率下的传输时间对比
plt.figure(figsize=(10, 6))
plt.plot(loss_rates, normal_loss, label='GBN Mechanism', marker='o')
plt.plot(loss_rates, congestion_loss, label='Congestion Control', marker='o')

# 设置图表标题和标签
plt.title('File Transfer Time vs Loss Rate', fontsize=14)
plt.xlabel('Packet Loss Rate (%)', fontsize=12)
plt.ylabel('Transfer Time (seconds)', fontsize=12)
plt.xticks(loss_rates)  # 设置X轴刻度
plt.legend()
plt.grid(True)
plt.show()

# 图2: 延时下的传输时间对比
plt.figure(figsize=(10, 6))
plt.plot(delays, normal_delay, label='GBN Mechanism', marker='o')
plt.plot(delays, congestion_delay, label='Congestion Control', marker='o')

# 设置图表标题和标签
plt.title('File Transfer Time vs Delay', fontsize=14)
plt.xlabel('Network Delay (ms)', fontsize=12)
plt.ylabel('Transfer Time (seconds)', fontsize=12)
plt.xticks(delays)  # 设置X轴刻度
plt.legend()
plt.grid(True)
plt.show()
