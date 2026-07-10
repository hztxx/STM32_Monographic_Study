#!/usr/bin/env python3
"""
性能数据可视化工具
读取JSON格式的性能报告并生成图表
"""

import json
import matplotlib.pyplot as plt
import numpy as np
from matplotlib import rcParams
# 配置中文字体
rcParams['font.sans-serif'] = ['SimHei']  # 用黑体显示中文
rcParams['axes.unicode_minus'] = False    # 正常显示负号

def load_performance_data(filename):
    """加载性能数据"""
    with open(filename, 'r', encoding='utf-8') as f:
        data = json.load(f)
    return data['performance_report']

def plot_performance_comparison(data):
    """绘制性能对比柱状图"""
    counters = data['counters']

    names = [c['name'] for c in counters]
    avg_cycles = [c['avg_cycles'] for c in counters]

    plt.figure(figsize=(12, 6))
    bars = plt.bar(range(len(names)), avg_cycles, color='steelblue', alpha=0.8)

    # 添加数值标签
    for i, bar in enumerate(bars):
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                f'{int(height)}',
                ha='center', va='bottom', fontsize=9)

    plt.xlabel('测量点', fontsize=12)
    plt.ylabel('平均周期数', fontsize=12)
    plt.title('性能对比分析', fontsize=14, fontweight='bold')
    plt.xticks(range(len(names)), names, rotation=45, ha='right')
    plt.grid(axis='y', alpha=0.3)
    plt.tight_layout()
    plt.savefig('performance_comparison.png', dpi=300)
    print("已生成: performance_comparison.png")

def plot_performance_distribution(data):
    """绘制性能分布饼图"""
    counters = data['counters']

    names = [c['name'] for c in counters]
    total_cycles = [c['total_cycles'] for c in counters]

    plt.figure(figsize=(10, 8))
    colors = plt.cm.Set3(np.linspace(0, 1, len(names)))

    wedges, texts, autotexts = plt.pie(total_cycles, labels=names, autopct='%1.1f%%',
                                        colors=colors, startangle=90)

    # 美化文本
    for text in texts:
        text.set_fontsize(10)
    for autotext in autotexts:
        autotext.set_color('white')
        autotext.set_fontweight('bold')
        autotext.set_fontsize(9)

    plt.title('性能占用分布', fontsize=14, fontweight='bold')
    plt.axis('equal')
    plt.tight_layout()
    plt.savefig('performance_distribution.png', dpi=300)
    print("已生成: performance_distribution.png")

def plot_performance_history(data):
    """绘制性能历史趋势图"""
    counters = data['counters']

    plt.figure(figsize=(14, 8))

    for counter in counters:
        name = counter['name']
        history = counter['history']

        # 过滤掉0值
        valid_history = [h for h in history if h > 0]
        if len(valid_history) > 0:
            plt.plot(range(len(valid_history)), valid_history, 
                    marker='o', label=name, linewidth=2, markersize=6)

    plt.xlabel('测量序号', fontsize=12)
    plt.ylabel('周期数', fontsize=12)
    plt.title('性能历史趋势', fontsize=14, fontweight='bold')
    plt.legend(loc='best', fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig('performance_history.png', dpi=300)
    print("已生成: performance_history.png")

def plot_min_max_range(data):
    """绘制最小/最大/平均值对比图"""
    counters = data['counters']

    names = [c['name'] for c in counters]
    min_cycles = [c['min_cycles'] for c in counters]
    avg_cycles = [c['avg_cycles'] for c in counters]
    max_cycles = [c['max_cycles'] for c in counters]

    x = np.arange(len(names))
    width = 0.25

    plt.figure(figsize=(14, 6))

    plt.bar(x - width, min_cycles, width, label='最小值', color='lightgreen', alpha=0.8)
    plt.bar(x, avg_cycles, width, label='平均值', color='steelblue', alpha=0.8)
    plt.bar(x + width, max_cycles, width, label='最大值', color='coral', alpha=0.8)

    plt.xlabel('测量点', fontsize=12)
    plt.ylabel('周期数', fontsize=12)
    plt.title('性能范围分析', fontsize=14, fontweight='bold')
    plt.xticks(x, names, rotation=45, ha='right')
    plt.legend(fontsize=10)
    plt.grid(axis='y', alpha=0.3)
    plt.tight_layout()
    plt.savefig('performance_range.png', dpi=300)
    print("已生成: performance_range.png")

def generate_html_report(data):
    """生成HTML报告"""
    # 使用不同的占位符避免与CSS花括号冲突
    html_template = """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>性能分析报告</title>
    <style>
        body {{
            font-family: 'Microsoft YaHei', Arial, sans-serif;
            margin: 20px;
            background-color: #f5f5f5;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }}
        h1 {{
            color: #333;
            border-bottom: 3px solid #4CAF50;
            padding-bottom: 10px;
        }}
        h2 {{
            color: #555;
            margin-top: 30px;
        }}
        table {{
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
        }}
        th, td {{
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }}
        th {{
            background-color: #4CAF50;
            color: white;
        }}
        tr:hover {{
            background-color: #f5f5f5;
        }}
        .chart {{
            margin: 30px 0;
            text-align: center;
        }}
        .chart img {{
            max-width: 100%;
            border: 1px solid #ddd;
            border-radius: 5px;
        }}
        .info-box {{
            background-color: #e3f2fd;
            padding: 15px;
            border-left: 4px solid #2196F3;
            margin: 20px 0;
        }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 性能分析报告</h1>

        <div class="info-box">
            <strong>CPU频率:</strong> {cpu_freq} MHz<br>
            <strong>测量点数量:</strong> {counter_count}<br>
            <strong>生成时间:</strong> <span id="datetime"></span>
        </div>

        <h2>📊 性能数据表</h2>
        <table>
            <thead>
                <tr>
                    <th>名称</th>
                    <th>调用次数</th>
                    <th>平均周期</th>
                    <th>最小周期</th>
                    <th>最大周期</th>
                    <th>平均时间 (μs)</th>
                </tr>
            </thead>
            <tbody>
                {table_rows}
            </tbody>
        </table>

        <h2>📈 性能图表</h2>

        <div class="chart">
            <h3>性能对比</h3>
            <img src="performance_comparison.png" alt="性能对比">
        </div>

        <div class="chart">
            <h3>性能分布</h3>
            <img src="performance_distribution.png" alt="性能分布">
        </div>

        <div class="chart">
            <h3>性能历史趋势</h3>
            <img src="performance_history.png" alt="性能历史">
        </div>

        <div class="chart">
            <h3>性能范围分析</h3>
            <img src="performance_range.png" alt="性能范围">
        </div>
    </div>

    <script>
        document.getElementById('datetime').textContent = new Date().toLocaleString('zh-CN');
    </script>
</body>
</html>
    """
    
    # 生成表格行
    table_rows = ""
    for counter in data['counters']:
        table_rows += f"""
                <tr>
                    <td>{counter['name']}</td>
                    <td>{counter['call_count']}</td>
                    <td>{counter['avg_cycles']}</td>
                    <td>{counter['min_cycles']}</td>
                    <td>{counter['max_cycles']}</td>
                    <td>{counter['avg_time_us']:.2f}</td>
                </tr>
        """

    # 使用format_map或者直接替换
    html = html_template.format(
        cpu_freq=data['cpu_freq_mhz'],
        counter_count=data['counter_count'],
        table_rows=table_rows
    )

    with open('performance_report.html', 'w', encoding='utf-8') as f:
        f.write(html)

    print("已生成: performance_report.html")

def main():
    """主函数"""
    import sys

    if len(sys.argv) < 2:
        print("用法: python visualize.py <performance_data.json>")
        sys.exit(1)

    filename = sys.argv[1]

    try:
        data = load_performance_data(filename)

        print("正在生成可视化图表...")
        plot_performance_comparison(data)
        plot_performance_distribution(data)
        plot_performance_history(data)
        plot_min_max_range(data)

        print("\n正在生成HTML报告...")
        generate_html_report(data)

        print("\n✅ 所有报告已生成完成!")
        print("请打开 performance_report.html 查看完整报告")

    except Exception as e:
        print(f"错误: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()