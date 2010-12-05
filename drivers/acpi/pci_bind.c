/*
 *  pci_bind.c - ACPI PCI Device Binding ($Revision: 2 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/acpi.h>
#include <linux/list.h>
#include <linux/pm_runtime.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#include "internal.h"

#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_bind");

static LIST_HEAD(acpi_pci_gpe_devs);

struct pci_gpe_dev {
	struct list_head node;
	struct pci_dev *dev;
	acpi_handle gpe_device;
	int gpe_number;
	struct work_struct work;
};

static void acpi_pci_wake_handler_work(struct work_struct *work)
{
	struct pci_gpe_dev *gpe_dev = container_of(work, struct pci_gpe_dev,
						   work);

	pci_check_pme_status(gpe_dev->dev);
	pm_runtime_resume(&gpe_dev->dev->dev);
	pci_wakeup_event(gpe_dev->dev);
	if (gpe_dev->dev->subordinate)
		pci_pme_wakeup_bus(gpe_dev->dev->subordinate);
}

static u32 acpi_pci_wake_handler(void *data)
{
	long gpe_number = (long) data;
	struct pci_gpe_dev *gpe_dev;

	list_for_each_entry(gpe_dev, &acpi_pci_gpe_devs, node) {
		if (gpe_number != gpe_dev->gpe_number)
			continue;

		schedule_work(&gpe_dev->work);
	}

	return ACPI_INTERRUPT_HANDLED;
}

static int acpi_pci_unbind(struct acpi_device *device)
{
	struct pci_dev *dev;

	dev = acpi_get_pci_dev(device->handle);
	if (!dev)
		goto out;

	if (device->wakeup.flags.valid) {
		struct pci_gpe_dev *gpe_dev;
		struct pci_gpe_dev *tmp;
		int gpe_count = 0;
		int gpe_number = device->wakeup.gpe_number;
		acpi_handle gpe_device = device->wakeup.gpe_device;

		list_for_each_entry_safe(gpe_dev, tmp, &acpi_pci_gpe_devs, node) {
			if (gpe_dev->dev == dev) {
				flush_work(&gpe_dev->work);
				list_del(&gpe_dev->node);
				kfree(gpe_dev);
			} else if (gpe_dev->gpe_number == gpe_number &&
				   gpe_dev->gpe_device == gpe_device) {
				gpe_count++;
			}
		}

		if (gpe_count == 0) {
			acpi_remove_gpe_handler(gpe_device, gpe_number,
						&acpi_pci_wake_handler);
		}
	}

	device_set_run_wake(&dev->dev, false);
	pci_acpi_remove_pm_notifier(device);

	if (!dev->subordinate)
		goto out;

	acpi_pci_irq_del_prt(dev->subordinate);

	device->ops.bind = NULL;
	device->ops.unbind = NULL;

out:
	pci_dev_put(dev);
	return 0;
}

static int acpi_pci_bind(struct acpi_device *device)
{
	acpi_status status;
	acpi_handle handle;
	struct pci_bus *bus;
	struct pci_dev *dev;

	dev = acpi_get_pci_dev(device->handle);
	if (!dev)
		return 0;

	pci_acpi_add_pm_notifier(device, dev);
	if (device->wakeup.flags.valid) {
		struct pci_gpe_dev *gpe_dev;
		acpi_handle gpe_device = device->wakeup.gpe_device;
		long gpe_number = device->wakeup.gpe_number;

		gpe_dev = kmalloc(sizeof(struct pci_gpe_dev), GFP_KERNEL);
		if (gpe_dev) {
			gpe_dev->dev = dev;
			gpe_dev->gpe_device = gpe_device;
			gpe_dev->gpe_number = gpe_number;
			INIT_WORK(&gpe_dev->work, acpi_pci_wake_handler_work);

			acpi_install_gpe_handler(gpe_device, gpe_number,
						 ACPI_GPE_LEVEL_TRIGGERED,
						 &acpi_pci_wake_handler,
						 (void *)gpe_number,
						 true);
			acpi_gpe_can_wake(device->wakeup.gpe_device,
					  device->wakeup.gpe_number);
			device->wakeup.flags.run_wake = 1;
			list_add_tail(&gpe_dev->node, &acpi_pci_gpe_devs);
		}
	}

	if (device->wakeup.flags.run_wake)
		device_set_run_wake(&dev->dev, true);

	/*
	 * Install the 'bind' function to facilitate callbacks for
	 * children of the P2P bridge.
	 */
	if (dev->subordinate) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Device %04x:%02x:%02x.%d is a PCI bridge\n",
				  pci_domain_nr(dev->bus), dev->bus->number,
				  PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn)));
		device->ops.bind = acpi_pci_bind;
		device->ops.unbind = acpi_pci_unbind;
	}

	acpi_power_transition(device, acpi_power_get_inferred_state(device));

	/*
	 * Evaluate and parse _PRT, if exists.  This code allows parsing of
	 * _PRT objects within the scope of non-bridge devices.  Note that
	 * _PRTs within the scope of a PCI bridge assume the bridge's
	 * subordinate bus number.
	 *
	 * TBD: Can _PRTs exist within the scope of non-bridge PCI devices?
	 */
	status = acpi_get_handle(device->handle, METHOD_NAME__PRT, &handle);
	if (ACPI_FAILURE(status))
		goto out;

	if (dev->subordinate)
		bus = dev->subordinate;
	else
		bus = dev->bus;

	acpi_pci_irq_add_prt(device->handle, bus);

out:
	pci_dev_put(dev);
	return 0;
}

int acpi_pci_bind_root(struct acpi_device *device)
{
	device->ops.bind = acpi_pci_bind;
	device->ops.unbind = acpi_pci_unbind;

	return 0;
}
