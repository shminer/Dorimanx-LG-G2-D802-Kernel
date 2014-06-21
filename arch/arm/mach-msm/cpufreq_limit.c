/*
 * Author: Paul Reioux aka Faux123 <reioux@gmail.com>
 *
 * Copyright 2012-2014 Paul Reioux
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_LCD_NOTIFY
#include <linux/lcd_notify.h>
#elif defined(CONFIG_POWERSUSPEND)
#include <linux/powersuspend.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include <mach/cpufreq.h>

#define MSM_CPUFREQ_LIMIT_MAJOR		1
#define MSM_CPUFREQ_LIMIT_MINOR		3

//#define DEBUG_CPU_LIMITER

uint32_t limited_max_freq = 2265600;

#if defined(CONFIG_LCD_NOTIFY) || \
	defined(CONFIG_POWERSUSPEND) || \
	defined(CONFIG_HAS_EARLYSUSPEND)

#define DEFAULT_SUSPEND_DEFER_TIME 5
static uint32_t suspend_max_freq = 0;
static uint32_t resume_max_freq = 2265600;

static struct cpu_limit {
	unsigned int suspend_defer_time;
	struct delayed_work suspend_work;
	struct work_struct resume_work;
	struct notifier_block notif;
} limit = {
	.suspend_defer_time = DEFAULT_SUSPEND_DEFER_TIME,
};
#endif

static int update_cpu_max_freq(int cpu, uint32_t max_freq)
{
	int ret = 0;

	ret = msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, max_freq);
	if (ret)
		return ret;

	limited_max_freq = max_freq;
#ifdef DEBUG_CPU_LIMITER
	if (max_freq != MSM_CPUFREQ_NO_LIMIT)
		pr_info("%s: Limiting cpu%d max frequency to %d\n",
			__func__, cpu, max_freq);
	else
		pr_info("%s: Max frequency reset for cpu%d\n",
			__func__, cpu);
#endif
	ret = cpufreq_update_policy(cpu);

	return ret;
}

#if defined(CONFIG_LCD_NOTIFY) || \
	defined(CONFIG_POWERSUSPEND) || \
	defined(CONFIG_HAS_EARLYSUSPEND)
static void msm_limit_suspend(struct work_struct *work)
{
	int cpu = 0, ret = 0;

	/* save current instance */
	resume_max_freq = limited_max_freq;

	/* not active, so exit */
	if (suspend_max_freq == 0)
		return;

	for_each_possible_cpu(cpu) {
		ret = update_cpu_max_freq(cpu, suspend_max_freq);
		if (ret)
			pr_debug("can't limit cpu%d max freq to %d\n",
				cpu, suspend_max_freq);
	}
	if (!ret)
		limited_max_freq = suspend_max_freq;
}

static void __ref msm_limit_resume(struct work_struct *work)
{
	int cpu = 0, ret = 0;

	/* not active, so exit */
	if (suspend_max_freq == 0)
		return;

	/* restore max allowed freq */
	for_each_possible_cpu(cpu) {
		ret = update_cpu_max_freq(cpu, resume_max_freq);
		if (ret)
			pr_debug("can't restore cpu%d max freq to %d\n",
				cpu, resume_max_freq);
	}
	if (!ret)
		limited_max_freq = resume_max_freq;
}

#ifdef CONFIG_LCD_NOTIFY
static void __msm_limit_suspend(void)
#elif defined(CONFIG_POWERSUSPEND)
static void __msm_limit_suspend(struct power_suspend *handler)
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void __msm_limit_suspend(struct early_suspend *handler)
#endif
{
	INIT_DELAYED_WORK(&limit.suspend_work, msm_limit_suspend);
	schedule_delayed_work_on(0, &limit.suspend_work,
			msecs_to_jiffies(limit.suspend_defer_time * 1000));
}

#ifdef CONFIG_LCD_NOTIFY
static void __msm_limit_resume(void)
#elif defined(CONFIG_POWERSUSPEND)
static void __msm_limit_resume(struct power_suspend *handler)
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void __msm_limit_resume(struct early_suspend *handler)
#endif
{
	cancel_delayed_work_sync(&limit.suspend_work);
	schedule_work_on(0, &limit.resume_work);
}

#ifdef CONFIG_LCD_NOTIFY
static int lcd_notifier_callback(struct notifier_block *nb,
                                 unsigned long event, void *data)
{
	switch (event) {
	case LCD_EVENT_ON_END:
	case LCD_EVENT_OFF_START:
		break;
	case LCD_EVENT_ON_START:
		__msm_limit_resume();
		break;
	case LCD_EVENT_OFF_END:
		__msm_limit_suspend();
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}
#elif defined(CONFIG_POWERSUSPEND)
static struct power_suspend msm_limit_power_suspend_driver = {
#else
static struct early_suspend msm_limit_early_suspend_driver = {
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 10,
#endif
	.suspend = __msm_limit_suspend,
	.resume = __msm_limit_resume,
};
#endif

static ssize_t msm_cpufreq_limit_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limited_max_freq);
}

static ssize_t msm_cpufreq_limit_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int cpu = 0;
	int ret = 0;

	unsigned int data;

	if(sscanf(buf, "%u\n", &data) == 1) {
		for_each_possible_cpu(cpu) {
			ret = update_cpu_max_freq(cpu, data);
			if (ret)
				pr_debug("can't limit cpu%d max freq to %d\n",
					cpu, data);
		}
		if (!ret)
			limited_max_freq = data;
	}

	return count;
}

#if defined(CONFIG_LCD_NOTIFY) || \
	defined(CONFIG_POWERSUSPEND) || \
	defined(CONFIG_HAS_EARLYSUSPEND)
static ssize_t suspend_max_freq_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", suspend_max_freq);
}

static ssize_t suspend_max_freq_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	ret = sscanf(buf, "%u", &val);
	if (ret != 1)
		return -EINVAL;

	if (val == 0)
		goto out;

	if (val < policy->min)
		val = policy->min;
	else if (val > policy->max)
		val = policy->max;
out:
	suspend_max_freq = val;

	return count;
}
#endif

static ssize_t msm_cpufreq_limit_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
			MSM_CPUFREQ_LIMIT_MAJOR, MSM_CPUFREQ_LIMIT_MINOR);
}

static struct kobj_attribute msm_cpufreq_limit_attribute = 
	__ATTR(cpufreq_limit, 0666,
		msm_cpufreq_limit_show,
		msm_cpufreq_limit_store);

static struct kobj_attribute msm_cpufreq_limit_version_attribute = 
	__ATTR(msm_cpufreq_limit_version, 0444,
		msm_cpufreq_limit_version_show,
		NULL);

#if defined(CONFIG_LCD_NOTIFY) || \
	defined(CONFIG_POWERSUSPEND) || \
	defined(CONFIG_HAS_EARLYSUSPEND)
static struct kobj_attribute suspend_max_freq_attribute =
	__ATTR(suspend_max_freq, 0666,
		suspend_max_freq_show,
		suspend_max_freq_store);
#endif

static struct attribute *msm_cpufreq_limit_attrs[] =
	{
		&msm_cpufreq_limit_attribute.attr,
#if defined(CONFIG_LCD_NOTIFY) || \
	defined(CONFIG_POWERSUSPEND) || \
	defined(CONFIG_HAS_EARLYSUSPEND)
		&suspend_max_freq_attribute.attr,
#endif
		&msm_cpufreq_limit_version_attribute.attr,
		NULL,
	};

static struct attribute_group msm_cpufreq_limit_attr_group =
	{
		.attrs = msm_cpufreq_limit_attrs,
	};

static struct kobject *msm_cpufreq_limit_kobj;

static int msm_cpufreq_limit_init(void)
{
	int sysfs_result;

	msm_cpufreq_limit_kobj =
		kobject_create_and_add("msm_cpufreq_limit", kernel_kobj);
	if (!msm_cpufreq_limit_kobj) {
		pr_err("%s msm_cpufreq_limit_kobj kobject create failed!\n",
			__func__);
		return -ENOMEM;
        }

	sysfs_result = sysfs_create_group(msm_cpufreq_limit_kobj,
			&msm_cpufreq_limit_attr_group);

        if (sysfs_result) {
		pr_info("%s msm_cpufreq_limit_kobj create failed!\n",
			__func__);
		kobject_put(msm_cpufreq_limit_kobj);
	}

#ifdef CONFIG_LCD_NOTIFY
	limit.notif.notifier_call = lcd_notifier_callback;
        if (lcd_register_client(&limit.notif) != 0) {
                pr_err("%s: Failed to register LCD notifier callback\n",
                       MSM_LIMIT);
		goto err_dev;
	}
#elif defined(CONFIG_POWERSUSPEND)
	register_power_suspend(&msm_limit_power_suspend_driver);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	register_early_suspend(&msm_limit_early_suspend_driver);
#endif

#if defined(CONFIG_LCD_NOTIFY) || \
	defined(CONFIG_POWERSUSPEND) || \
	defined(CONFIG_HAS_EARLYSUSPEND)
	INIT_DELAYED_WORK(&limit.suspend_work, msm_limit_suspend);
	INIT_WORK(&limit.resume_work, msm_limit_resume);
#endif

	return sysfs_result;
}

static void msm_cpufreq_limit_exit(void)
{
	if (msm_cpufreq_limit_kobj != NULL)
		kobject_put(msm_cpufreq_limit_kobj);

#if defined(CONFIG_LCD_NOTIFY) || \
	defined(CONFIG_POWERSUSPEND) || \
	defined(CONFIG_HAS_EARLYSUSPEND)
	cancel_work_sync(&limit.resume_work);
	cancel_delayed_work_sync(&limit.suspend_work);
#endif

#ifdef CONFIG_LCD_NOTIFY
	lcd_unregister_client(&limit.notif);
	limit.notif.notifier_call = NULL;
#elif defined(CONFIG_POWERSUSPEND)
	unregister_power_suspend(&msm_limit_power_suspend_driver);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&msm_limit_early_suspend_driver);
#endif
}

module_init(msm_cpufreq_limit_init);
module_exit(msm_cpufreq_limit_exit);
MODULE_LICENSE("GPL v2"); 
MODULE_AUTHOR("Paul Reioux <reioux@gmail.com>");
MODULE_AUTHOR("Dorimanx <yuri@bynet.co.il>");
MODULE_DESCRIPTION("Krait CPU frequency Limit Driver");

