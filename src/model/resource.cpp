/***************************************************************************
 *                                                                         *
 * Copyright (C) 2007-2015 by frePPLe bvba                                 *
 *                                                                         *
 * This library is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU Affero General Public License as published   *
 * by the Free Software Foundation; either version 3 of the License, or    *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This library is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
 * GNU Affero General Public License for more details.                     *
 *                                                                         *
 * You should have received a copy of the GNU Affero General Public        *
 * License along with this program.                                        *
 * If not, see <http://www.gnu.org/licenses/>.                             *
 *                                                                         *
 ***************************************************************************/

#define FREPPLE_CORE
#include "frepple/model.h"

namespace frepple
{

template<class Resource> Tree<string> utils::HasName<Resource>::st;
const MetaCategory* Resource::metadata;
const MetaClass* ResourceDefault::metadata;
const MetaClass* ResourceInfinite::metadata;
const MetaClass* ResourceBuckets::metadata;

Duration Resource::defaultMaxEarly(100*86400L);


int Resource::initialize()
{
  // Initialize the metadata
  metadata = MetaCategory::registerCategory<Resource>("resource", "resources", reader, finder);
  registerFields<Resource>(const_cast<MetaCategory*>(metadata));

  // Initialize the Python class
  FreppleCategory<Resource>::getPythonType().addMethod("plan", Resource::plan, METH_VARARGS,
      "Return an iterator with tuples representing the resource plan in each time bucket");
  return FreppleCategory<Resource>::initialize();
}


int ResourceDefault::initialize()
{
  // Initialize the metadata
  ResourceDefault::metadata = MetaClass::registerClass<ResourceDefault>(
    "resource", "resource_default",
    Object::create<ResourceDefault>,
    true
    );

  // Initialize the Python class
  return FreppleClass<ResourceDefault,Resource>::initialize();
}


int ResourceInfinite::initialize()
{
  // Initialize the metadata
  ResourceInfinite::metadata = MetaClass::registerClass<ResourceInfinite>(
    "resource", "resource_infinite",
    Object::create<ResourceInfinite>
    );

  // Initialize the Python class
  return FreppleClass<ResourceInfinite,Resource>::initialize();
}


int ResourceBuckets::initialize()
{
  // Initialize the metadata
  ResourceBuckets::metadata = MetaClass::registerClass<ResourceBuckets>(
    "resource",
    "resource_buckets",
    Object::create<ResourceBuckets>);

  // Initialize the Python class
  return FreppleClass<ResourceBuckets,Resource>::initialize();
}


void Resource::inspect(const string msg) const
{
  logger << "Inspecting resource " << getName() << ": ";
  if (!msg.empty()) logger  << msg;
  logger << endl;

  OperationPlan *opplan = nullptr;
  for (loadplanlist::const_iterator oo = getLoadPlans().begin();
    oo != getLoadPlans().end();
    ++oo)
  {
    logger << "  " << oo->getDate()
      << " qty:" << oo->getQuantity()
      << ", oh:" << oo->getOnhand();
    switch (oo->getEventType())
    {
    case 1:
      opplan = oo->getOperationPlan();
      logger << ", id: " << opplan->getIdentifier()
        << ", oper:" << opplan->getOperation()
        << ", quantity: " << opplan->getQuantity()
        << ", dates: " << opplan->getStart();
      if (opplan->getSetupEnd() != opplan->getStart())
        logger << " / " << opplan->getSetupEnd();
      logger << " / " << opplan->getEnd();
      if (!opplan->getProposed())
        logger << ", " << opplan->getStatus();
      logger << endl;
      break;
    case 2:
      logger << ", set onhand to " << oo->getOnhand() << endl;
      break;
    case 3:
      logger << ", update minimum to " << oo->getMin() << endl;
      break;
    case 4:
      logger << ", update maximum to " << oo->getMax() << endl;
      break;
    }
  }
}


void Resource::setMaximum(double m)
{
  if (m < 0)
    throw DataException("Maximum capacity for resource '" + getName() + "' must be postive");

  // There is already a maximum calendar.
  if (size_max_cal)
  {
    // We update the field, but don't use it yet.
    size_max = m;
    return;
  }

  // Mark as changed
  setChanged();

  // Set field
  size_max = m;

  // Create or update a single timeline max event
  for (loadplanlist::iterator oo=loadplans.begin(); oo!=loadplans.end(); oo++)
    if (oo->getEventType() == 4)
    {
      // Update existing event
      static_cast<loadplanlist::EventMaxQuantity *>(&*oo)->setMax(size_max);
      return;
    }
  // Create new event
  loadplanlist::EventMaxQuantity *newEvent =
    new loadplanlist::EventMaxQuantity(Date::infinitePast, &loadplans, size_max);
  loadplans.insert(newEvent);
}


void Resource::setMaximumCalendar(Calendar* c)
{
  // Resetting the same calendar
  if (size_max_cal == c) return;

  // Mark as changed
  setChanged();

  // Remove the current max events.
  for (loadplanlist::iterator oo=loadplans.begin(); oo!=loadplans.end(); )
    if (oo->getEventType() == 4)
    {
      loadplans.erase(&(*oo));
      delete &(*(oo++));
    }
    else ++oo;

  // Null pointer passed. Change back to time independent maximum size.
  if (!c)
  {
    setMaximum(size_max);
    return;
  }

  // Create timeline structures for every bucket.
  size_max_cal = c;
  double curMax = 0.0;
  for (CalendarDefault::EventIterator x(size_max_cal); x.getDate()<Date::infiniteFuture; ++x)
    if (curMax != x.getValue())
    {
      curMax = x.getValue();
      loadplanlist::EventMaxQuantity *newBucket =
        new loadplanlist::EventMaxQuantity(x.getDate(), &loadplans, curMax);
      loadplans.insert(newBucket);
    }
}


void ResourceBuckets::setMaximumCalendar(Calendar* c)
{
  // Resetting the same calendar
  if (size_max_cal == c) return;

  // Mark as changed
  setChanged();

  // Remove the current set-onhand events.
  for (loadplanlist::iterator oo = loadplans.begin(); oo != loadplans.end(); )
  {
    loadplanlist::Event *tmp = &*oo;
    ++oo;
    if (tmp->getEventType() == 2)
    {
      loadplans.erase(tmp);
      delete tmp;
    }
  }

  // Create timeline structures for every bucket.
  size_max_cal = c;
  double v = 0.0;
  for (CalendarDefault::EventIterator x(size_max_cal); x.getDate()<Date::infiniteFuture; ++x)
    if (v != x.getValue())
    {
      v = x.getValue();
      loadplanlist::EventSetOnhand *newBucket =
        new loadplanlist::EventSetOnhand(x.getDate(), v);
      loadplans.insert(newBucket);
    }
}


void Resource::deleteOperationPlans(bool deleteLocked)
{
  // Delete the operationplans
  for (loadlist::iterator i=loads.begin(); i!=loads.end(); ++i)
    OperationPlan::deleteOperationPlans(i->getOperation(),deleteLocked);

  // Mark to recompute the problems
  setChanged();
}


Resource::~Resource()
{
  // Delete all operationplans
  // An alternative logic would be to delete only the loadplans for this
  // resource and leave the rest of the plan untouched. The currently
  // implemented method is way more drastic...
  deleteOperationPlans(true);

  // The Load and ResourceSkill objects are automatically deleted by the
  // destructor of the Association list class.

  // Clean up references on the itemsupplier and itemdistribution models
  for (Item::iterator itm_iter = Item::begin(); itm_iter != Item::end(); ++itm_iter)
  {
    Item::supplierlist::const_iterator itmsup_iter = itm_iter->getSupplierIterator();
    while (ItemSupplier* itmsup = itmsup_iter.next())
      if (itmsup->getResource() == this)
        itmsup->setResource(nullptr);
    Item::distributionIterator  itmdist_iter = itm_iter->getDistributionIterator();
    while (ItemDistribution* itmdist = itmdist_iter.next())
      if (itmdist->getResource() == this)
        itmdist->setResource(nullptr);
  }
}


extern "C" PyObject* Resource::plan(PyObject *self, PyObject *args)
{
  // Get the resource model
  Resource* resource = static_cast<Resource*>(self);

  // Parse the Python arguments
  PyObject* buckets = nullptr;
  int ok = PyArg_ParseTuple(args, "O:plan", &buckets);
  if (!ok) return nullptr;

  // Validate that the argument supports iteration.
  PyObject* iter = PyObject_GetIter(buckets);
  if (!iter)
  {
    PyErr_Format(PyExc_AttributeError,"Argument to resource.plan() must support iteration");
    return nullptr;
  }

  // Return the iterator
  return new Resource::PlanIterator(resource, iter);
}


int Resource::PlanIterator::initialize()
{
  // Initialize the type
  PythonType& x = PythonExtension<Resource::PlanIterator>::getPythonType();
  x.setName("resourceplanIterator");
  x.setDoc("frePPLe iterator for resourceplan");
  x.supportiter();
  return x.typeReady();
}


Resource::PlanIterator::PlanIterator(Resource* r, PyObject* o) : bucketiterator(o)
{
  // Start date of the first bucket
  end_date = PyIter_Next(bucketiterator);
  if (!end_date)
    throw LogicException("Expecting at least two dates as argument");

  // Collect all subresources
  if (!r)
  {
    bucketiterator = nullptr;
    throw LogicException("Creating resource plan iterator for nullptr resource");
  }
  else if (r->isGroup())
  {
    for (Resource::memberRecursiveIterator i(r); !i.empty(); ++i)
      if (!i->isGroup())
      {
        _res tmp;
        tmp.res = &*i;
        res_list.push_back(tmp);
      }
  }
  else
  {
    _res tmp;
    tmp.res = r;
    res_list.push_back(tmp);
  }

  // Initialize the iterator for all resources
  for (auto i = res_list.begin(); i != res_list.end(); ++i)
  {
    i->ldplaniter = Resource::loadplanlist::iterator(i->res->getLoadPlans().begin());
    i->bucketized = (r->getType() == *ResourceBuckets::metadata);
    i->cur_date = PythonData(end_date).getDate();
    i->setup_loadplan = nullptr;
    i->prev_date = i->cur_date;
    i->cur_size = 0.0;
    i->cur_setup = 0.0;
    i->cur_load = 0.0;

    if (i->bucketized)
    {
      // Scan forward to the first relevant bucket
      while (
        i->ldplaniter != i->res->getLoadPlans().end() 
        && (i->ldplaniter->getEventType() != 2 || i->ldplaniter->getDate() < i->cur_date)
        )
          ++(i->ldplaniter);
    }
    else
    {
      // Initialize unavailability iterators
      i->prev_value = true;
      if (i->res->getLocation() && i->res->getLocation()->getAvailable())
      {
        i->unavailLocIter = Calendar::EventIterator(i->res->getLocation()->getAvailable(), i->cur_date);
        i->prev_value = (i->unavailLocIter.getCalendar()->getValue(i->cur_date) != 0);
      }
      if (i->res->getAvailable())
      {
        i->unavailIter = Calendar::EventIterator(i->res->getAvailable(), i->cur_date);
        if (i->prev_value)
          i->prev_value = (i->unavailIter.getCalendar()->getValue(i->cur_date) != 0);
      }

      // Advance loadplan iterator just beyond the starting date
      while (i->ldplaniter != i->res->getLoadPlans().end() && i->ldplaniter->getDate() <= i->cur_date)
      {
        unsigned short tp = i->ldplaniter->getEventType();
        if (tp == 4)
          // New max size
          i->cur_size = i->ldplaniter->getMax();
        else if (tp == 1)
        {
          const LoadPlan* ldplan = dynamic_cast<const LoadPlan*>(&*(i->ldplaniter));
          if (
            ldplan->getOperationPlan()->getSetupEnd() != ldplan->getOperationPlan()->getStart()
            && ldplan->getQuantity() > 0
            )
            i->setup_loadplan = ldplan;
          else
            i->setup_loadplan = nullptr;
          i->cur_load = ldplan->getOnhand();
        }
        ++(i->ldplaniter);
      }
    }
  }
}


Resource::PlanIterator::~PlanIterator()
{
  if (bucketiterator)
    Py_DECREF(bucketiterator);
  if (start_date)
    Py_DECREF(start_date);
  if (end_date)
    Py_DECREF(end_date);
}


void Resource::PlanIterator::update(Resource::PlanIterator::_res* i, Date till)
{
  long timedelta;
  if (i->unavailIter.getCalendar() || i->unavailLocIter.getCalendar())
  {
    // Advance till the iterator exceeds the target date
    while (
      (i->unavailLocIter.getCalendar() && i->unavailLocIter.getDate() <= till)
      || (i->unavailIter.getCalendar() && i->unavailIter.getDate() <= till)
      )
    {
      if (i->unavailIter.getCalendar() &&
        (!i->unavailLocIter.getCalendar() || i->unavailIter.getDate() < i->unavailLocIter.getDate()))
      {
        timedelta = i->unavailIter.getDate() - i->prev_date;
        i->prev_date = i->unavailIter.getDate();
      }
      else
      {
        timedelta = i->unavailLocIter.getDate() - i->prev_date;
        i->prev_date = i->unavailLocIter.getDate();
      }
      if (i->prev_value)
      {
        bucket_available += i->cur_size * timedelta / 3600;
        bucket_load += i->cur_load * timedelta / 3600;
        bucket_setup += i->cur_setup * timedelta / 3600;
      }
      else
        bucket_unavailable += i->cur_size * timedelta / 3600;
      if (i->unavailIter.getCalendar() && i->unavailIter.getDate() == i->prev_date)
      {
        // Increment only resource unavailability iterator        
        ++(i->unavailIter);
        if (i->unavailLocIter.getCalendar() && i->unavailLocIter.getDate() == i->prev_date)
          // Increment both resource and location unavailability iterators
          ++(i->unavailLocIter);
      }
      else if (i->unavailLocIter.getCalendar() && i->unavailLocIter.getDate() == i->prev_date)
        // Increment only location unavailability iterator
        ++(i->unavailLocIter);
      else
        throw LogicException("Unreachable code");
      i->prev_value = true;
      if (i->unavailIter.getCalendar())
        i->prev_value = (i->unavailIter.getCalendar()->getValue(i->prev_date) != 0);
      if (i->unavailLocIter.getCalendar() && i->prev_value)
        i->prev_value = (i->unavailLocIter.getCalendar()->getValue(i->prev_date) != 0);
    }
    // Account for time period finishing at the "till" date
    timedelta = till - i->prev_date;
    if (i->prev_value)
    {
      bucket_available += i->cur_size * timedelta / 3600;
      bucket_load += i->cur_load * timedelta / 3600;
      bucket_setup += i->cur_setup * timedelta / 3600;
    }
    else
      bucket_unavailable += i->cur_size * timedelta / 3600;
  }
  else
  {
    // All time is available on this resource
    timedelta = till - i->prev_date;
    bucket_available += i->cur_size * timedelta / 3600;
    bucket_load += i->cur_load  * timedelta / 3600;
    bucket_setup += i->cur_setup * timedelta / 3600;
  }
  // Remember till which date we already have reported
  i->prev_date = till;
}


PyObject* Resource::PlanIterator::iternext()
{
  // Reset counters
  bucket_available = 0.0;
  bucket_unavailable = 0.0;
  bucket_load = 0.0;
  bucket_setup = 0.0;

  // Get the start and end date of the current bucket
  if (start_date)
    Py_DECREF(start_date);
  start_date = end_date;
  end_date = PyIter_Next(bucketiterator);
  if (!end_date)
    return nullptr;
  Date cpp_start_date = PythonData(end_date).getDate();
  Date cpp_end_date = PythonData(end_date).getDate();

  for (auto i = res_list.begin(); i != res_list.end(); ++i)
  {
    i->cur_date = cpp_end_date;
    if (i->bucketized)
    {
      // Bucketized resource
      while (
        i->ldplaniter != i->res->getLoadPlans().end()
        && i->ldplaniter->getDate() < cpp_end_date
        )
      {
        // At this point ldplaniter points to a bucket start event in the
        // current reporting bucket
        bucket_available += i->ldplaniter->getOnhand();

        // Advance the loadplan iterator to the start of the next bucket
        ++(i->ldplaniter);
        while (
          i->ldplaniter != i->res->getLoadPlans().end()
          && i->ldplaniter->getEventType() != 2
          )
        {
          if (i->ldplaniter->getEventType() == 1)
            bucket_load -= i->ldplaniter->getQuantity();
          ++(i->ldplaniter);
        }
      }
    }
    else
    {
      // Default resource

      // Measure from beginning of the bucket till the first event in this bucket
      if (i->ldplaniter != i->res->getLoadPlans().end() && i->ldplaniter->getDate() < i->cur_date)
        update(&*i, i->ldplaniter->getDate());

      // Advance the loadplan iterator to the next event date
      while (i->ldplaniter != i->res->getLoadPlans().end() && i->ldplaniter->getDate() <= i->cur_date)
      {
        // Measure from the previous event till the current one
        update(&*i, i->ldplaniter->getDate());

        // Process the event
        unsigned short tp = i->ldplaniter->getEventType();
        if (tp == 4)
          // New max size
          i->cur_size = i->ldplaniter->getMax();
        else if (tp == 1)
        {
          const LoadPlan* ldplan = dynamic_cast<const LoadPlan*>(&*(i->ldplaniter));
          assert(ldplan);
          if (
            ldplan->getOperationPlan()->getSetupEnd() != ldplan->getOperationPlan()->getStart()
            && ldplan->getQuantity() > 0
            )
            i->setup_loadplan = ldplan;
          else
            i->setup_loadplan = nullptr;
          i->cur_load = ldplan->getOnhand();
        }

        // Move to the next event
        ++(i->ldplaniter);
      }

      // Measure from the previous event till the end of the bucket
      update(&*i, i->cur_date);
    }
  }

  // Skip empty buckets
  if (!bucket_available && !bucket_unavailable && !bucket_load && !bucket_setup)
    return iternext();

  // Return the result
  return Py_BuildValue("{s:O,s:O,s:d,s:d,s:d,s:d,s:d}",
    "start", start_date,
    "end", end_date,
    "available", bucket_available,
    "load", bucket_load,
    "unavailable", bucket_unavailable,
    "setup", bucket_setup,
    "free", bucket_available - bucket_load - bucket_setup);
}


bool Resource::hasSkill(Skill* s, Date st, Date nd, ResourceSkill** resSkill) const
{
  if (!s)
  {
    if (resSkill)
      *resSkill = nullptr;
    return false;
  }

  Resource::skilllist::const_iterator i = getSkills();
  while (ResourceSkill *rs = i.next())
  {
    if (rs->getSkill() == s
      && st >= rs->getEffective().getStart()
      && nd <= rs->getEffective().getEnd())
    {
      if (resSkill)
        *resSkill = rs;
      return true;
    }
  }
  if (resSkill)
    *resSkill = nullptr;
  return false;
}


void Resource::setSetupMatrix(SetupMatrix *s)
{
  if (getType() == *ResourceBuckets::metadata)
    throw DataException("No setup calendar can be defined on bucketized resources");
  setupmatrix = s;
}


PooledString Resource::getSetupAt(Date d, bool inclusive) const
{
  PooledString tmp = setup;
  Date latestSetupEnd;
  for (auto i = getLoadPlans().begin(); i != getLoadPlans().end(); ++i)
  {
    if (i->getDate() > d || (!inclusive && i->getDate() == d))
      break;
    if (i->getEventType() != 1 || i->getQuantity() >= 0)
      continue;
    auto ldpln = static_cast<const LoadPlan*>(&*i);
    if (!ldpln->getLoad()->setup.empty() && ldpln->getResource()->getSetupMatrix())
    {
      Date t = ldpln->getOperationPlan()->getSetupEnd();
      if (t > latestSetupEnd && (t < d || (inclusive && t == d)))
      {
        tmp = ldpln->getLoad()->setup;
        latestSetupEnd = ldpln->getOperationPlan()->getSetupEnd();
      }
    }
  }
  return tmp;
}


void Resource::updateSetupTime() const
{
  if (setupmatrix)
    for (auto qq = getLoadPlans().begin(); qq != getLoadPlans().end(); ++qq)
      if (qq->getEventType() == 1 && qq->getQuantity() < 0.0)
        qq->getOperationPlan()->updateSetupTime();
}

}
